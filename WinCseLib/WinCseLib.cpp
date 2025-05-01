#include "WinCseLib.h"
#include <iostream>
#include <fstream>
#include <dbghelp.h>


namespace CSELIB {

void AbnormalEnd(PCWSTR file, int line, PCWSTR func, int signum)
{
	const auto errno_v = errno;

	SYSTEMTIME st;
	::GetLocalTime(&st);

	wchar_t szTempPath[MAX_PATH];
	::GetTempPathW(MAX_PATH, szTempPath);

	std::wstring tempPath{ szTempPath };

	const DWORD pid = ::GetCurrentProcessId();
	const DWORD tid = ::GetCurrentThreadId();

	std::wostringstream ss;
	ss << tempPath;

	if (std::filesystem::is_directory(tempPath + L"WinCse"))
	{
		ss << L"WinCse\\";
	}
	else
	{
		ss << L"WinCse-";
	}

	ss << L"abend-";
	ss << std::setw(4) << std::setfill(L'0') << st.wYear;
	ss << std::setw(2) << std::setfill(L'0') << st.wMonth;
	ss << std::setw(2) << std::setfill(L'0') << st.wDay;
	ss << L'-';
	ss << pid;
	ss << L'-';
	ss << tid;
	ss << L".log";

	const auto strPath{ ss.str() };
	
	std::wcerr << L"output file=" << strPath << std::endl;
	std::wofstream ofs{ strPath, std::ios_base::app };

	ss.str(L"");
	ss << L"cause; ";
	ss << file;
	ss << L"(";
	ss << line;
	ss << L"); signum=";
	ss << signum;
	ss << L"; ";
	ss << func;
	ss << std::endl;
	ss << L"errno=";
	ss << errno_v;
	ss << std::endl;
	ss << L"GetLastError()=";
	ss << ::GetLastError();
	ss << std::endl;

	const auto causeStr{ ss.str() };

#ifdef _DEBUG
	::OutputDebugStringW(causeStr.c_str());
#endif

	if (ofs)
	{
		ofs << causeStr;
	}

	//
	const int maxFrames = 62;
	void* stack[maxFrames];
	HANDLE hProcess = ::GetCurrentProcess();
	::SymInitialize(hProcess, NULL, TRUE);

	USHORT frames = ::CaptureStackBackTrace(0, maxFrames, stack, NULL);

	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (USHORT i = 0; i < frames; i++)
	{
		::SymFromAddr(hProcess, (DWORD64)(stack[i]), 0, symbol);

		ss.str(L"");
		ss << frames - i - 1;
		ss << L": ";
		ss << symbol->Name;
		ss << L" - 0x";
		ss << symbol->Address;
		ss << std::endl;

		const std::wstring ss_str{ ss.str() };

#ifdef _DEBUG
		::OutputDebugStringW(ss_str.c_str());
#endif

		if (ofs)
		{
			ofs << ss_str;
		}
	}

	if (ofs)
	{
		ofs << std::endl;
	}

	free(symbol);

	ofs.close();

	::SymCleanup(hProcess);

	abort();
}

// ファイル名から FSP_FSCTL_DIR_INFO の shared_ptr を生成し、FileInfo と FileNameBuf を埋めて返却
//
// 引数の argFileName は以下を想定している
// 
//	1) Windows ルート・ディレクトリ			"/"					利用箇所限定
//	2) ドット・エントリ						".", ".."
//	3) バケット or ディレクトリ名			"dir/"				"/" 終端されていること
//	4) ファイル名							"file.txt
// 
//	--> "/" 終端のときにバケットなのかディレクトリなのか判断できないので、FileTypeEnum も引数に必要となる
// 
// この値は FSP_FSCTL_FILE_INFO.FileAttributes の意味と同期している必要がある
// また、DirInfoPtr にはファイル名が 2 つあり、それぞれ以下の意味となる
// 
//	dirInfoPtr->FileName					上記と同じ値
//	dirInfoPtr->FileNameBuf					ディレクトリの場合は "/" 終端が削除される
// 
// 通常は FileName の値を利用するが、ReadDirectory() で Fsp に送信するデータは
// FSP_FSCTL_DIR_INFO.FileNameBuf となっている。
//

static std::atomic<int> gAllocId{ 0 };

DirInfoPtr allocBasicDirInfo(const std::wstring& argFileName, CSELIB::FileTypeEnum argFileType, const FSP_FSCTL_FILE_INFO& argFileInfo)
{
	APP_ASSERT(!argFileName.empty());
	APP_ASSERT(argFileType != FileTypeEnum::None);
	APP_ASSERT(argFileInfo.LastAccessTime != 0ULL);
	APP_ASSERT(argFileName.find(L'\\') == std::string::npos);		// "\\" は含まれないはず

	// 名前からチェック

	if (argFileName == L"/")
	{
		// "/" はルート

		APP_ASSERT(FA_IS_DIR(argFileInfo.FileAttributes));
		APP_ASSERT(argFileType == FileTypeEnum::RootDirectory);
	}
	else if (argFileName == L"." || argFileName == L"..")
	{
		// ドット・エントリはディレクトリ

		APP_ASSERT(FA_IS_DIR(argFileInfo.FileAttributes));
		APP_ASSERT(argFileType == FileTypeEnum::DirectoryObject);
	}
	else if (argFileName.back() == L'/')
	{
		// "/" 終端はディレクトリかバケット

		APP_ASSERT(FA_IS_DIR(argFileInfo.FileAttributes));
		APP_ASSERT(argFileType == FileTypeEnum::DirectoryObject || argFileType == FileTypeEnum::Bucket);
	}
	else
	{
		// それ以外はファイル

		APP_ASSERT(!FA_IS_DIR(argFileInfo.FileAttributes));
		APP_ASSERT(argFileType == FileTypeEnum::FileObject);
	}

	auto fileNameBuf{ argFileName };				// ... FSP_FSCTL_DIR_INFO に保存するファイル名

	// 属性からチェック

	if (FA_IS_DIR(argFileInfo.FileAttributes))
	{
		if (fileNameBuf == L"/")
		{
			// CSDriver::getDirInfoByWinPath() から遷移するときのみここを通過する
			// 検査不要
		}
		else if (fileNameBuf == L"." || fileNameBuf == L"..")
		{
			// ドット・エントリなので検査不要
		}
		else
		{
			// ディレクトリなので "/" 終端しているはず

			APP_ASSERT(fileNameBuf.back() == L'/');

			// FSP_FSCTL_DIR_INFO への転記用に "/" を削除

			fileNameBuf.pop_back();
		}
	}

	if (argFileType != FileTypeEnum::RootDirectory)
	{
		// FSP_FSCTL_DIR_INFO.FileNameBuf には "/" を含めない

		APP_ASSERT(fileNameBuf.find(L'/') == std::string::npos);
	}

	// FSP_FSCTL_DIR_INFO を生成

	const auto keyLen = fileNameBuf.length();
	const auto keyLenBytes = keyLen * sizeof(WCHAR);
	const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
	const auto dirInfoSize = offFileNameBuf + keyLenBytes;
	const auto allocSize = dirInfoSize + sizeof(WCHAR);

	FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)calloc(1, allocSize);
	APP_ASSERT(dirInfo);

	dirInfo->Size = (UINT16)dirInfoSize;

	dirInfo->FileInfo = argFileInfo;

	// GetFileInfoInternal() で FileInfo が設定されていることもあるので
	// 以降の項目については、未設定の時のみ初期化する

	if (!dirInfo->FileInfo.ChangeTime)
	{
		dirInfo->FileInfo.ChangeTime = dirInfo->FileInfo.LastWriteTime;
	}

	if (!dirInfo->FileInfo.IndexNumber)
	{
		dirInfo->FileInfo.IndexNumber = HashString(fileNameBuf);
	}

	if (dirInfo->FileInfo.FileSize && !dirInfo->FileInfo.AllocationSize)
	{
		//dirInfo->FileInfo.AllocationSize = (dirInfo->FileInfo.FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
		dirInfo->FileInfo.AllocationSize = ALIGN_TO_UNIT(dirInfo->FileInfo.FileSize);
	}

	//
	// 実行時にエラーとなる (argBuffer is too small)
	// 
	// おそらく、FSP_FSCTL_DIR_INFO.FileNameBuf は [] として定義されているため
	// wcscpy_s では 0 byte 領域へのバッファ・オーバーフローとして認識されて
	// しまうのではないかと思う
	// 
	//wcscpy_s(dirInfo->FileNameBuf, wkeyLen, wkey.c_str());

	memcpy(dirInfo->FileNameBuf, fileNameBuf.c_str(), keyLenBytes);

	const int allocId = ++gAllocId;

	return std::make_shared<DirInfo>(allocId, dirInfo, argFileName, argFileType);
}

NTSTATUS HandleToSecurityInfo(HANDLE Handle,
	PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize /* nullable */)
{
	DWORD SecurityDescriptorSizeNeeded = 0;

	if (0 != argSecurityDescriptorSize)
	{
		if (!::GetKernelObjectSecurity(Handle,
			OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			argSecurityDescriptor, (DWORD)*argSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
		{
			*argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
			return FspNtStatusFromWin32(::GetLastError());
		}

		*argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
	}

	return STATUS_SUCCESS;
}

int NamedWorkersToMap(NamedWorker workers[], std::map<std::wstring, IWorker*>* pWorkerMap)
{
	if (!workers)
	{
		return -1;
	}

	int num = 0;

	NamedWorker* cur = workers;
	while (cur->name)
	{
		pWorkerMap->emplace(cur->name, cur->worker);
		cur++;
		num++;
	}

	return num;
}

int GetIniIntW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, int defaultValue, int minValue, int maxValue)
{
	LastErrorBackup _backup;

	const auto section = argSection.c_str();

	APP_ASSERT(section);
	APP_ASSERT(section[0]);

	int ret = ::GetPrivateProfileIntW(section, keyName, defaultValue, confPath.c_str());
	if (ret < minValue)
	{
		ret = minValue;
	}
	else if (ret > maxValue)
	{
		ret = maxValue;
	}

	return ret;
}

bool GetIniBoolW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, bool defaultValue)
{
	LastErrorBackup _backup;

	const auto section = argSection.c_str();

	APP_ASSERT(section);
	APP_ASSERT(section[0]);

	int ret = ::GetPrivateProfileIntW(section, keyName, -1, confPath.c_str());
	if (ret == -1)
	{
		return defaultValue;
	}

	return ret ? true : false;
}

#define INI_LINE_BUFSIZ		(1024)

bool GetIniStringW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, std::wstring* pValue)
{
	LastErrorBackup _backup;

	const auto section = argSection.c_str();

	APP_ASSERT(section);
	APP_ASSERT(section[0]);

	std::vector<WCHAR> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringW(section, keyName, L"", buf.data(), (DWORD)buf.size(), confPath.c_str());
	const auto lerr = ::GetLastError();

	if (lerr != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::wstring(buf.data());

	return true;
}

//
// LogBlock
//
thread_local int LogBlock::mDepth = 0;

LogBlock::LogBlock(PCWSTR argFile, int argLine, PCWSTR argFunc) noexcept
	:
	mFile(argFile), mLine(argLine), mFunc(argFunc)
{
	GetLogger()->traceW_impl(mDepth, mFile, mLine, mFunc, L"{enter}");
	mDepth++;
}

LogBlock::~LogBlock()
{
	mDepth--;
	GetLogger()->traceW_impl(mDepth, mFile, -1, mFunc, L"{leave}");
}

int LogBlock::depth() const noexcept
{
	return mDepth;
}

} // namespace CSELIB

// EOF
