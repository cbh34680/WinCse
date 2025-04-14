#include "WinCseLib.h"
#include <fstream>
#include <filesystem>
#include <dbghelp.h>

#pragma comment(lib, "Crypt32.lib")             // CryptBinaryToStringA
#pragma comment(lib, "Dbghelp.lib")             // SymInitialize


namespace WCSE {

void AbnormalEnd(PCSTR file, int line, PCSTR func, int signum)
{
	wchar_t szTempPath[MAX_PATH];
	::GetTempPathW(MAX_PATH, szTempPath);
	std::wstring tempPath{ szTempPath };

	const DWORD pid = ::GetCurrentProcessId();
	const DWORD tid = ::GetCurrentThreadId();

	std::wostringstream ssPath;
	ssPath << tempPath;

	if (std::filesystem::is_directory(tempPath + L"WinCse"))
	{
		ssPath << L"WinCse\\";
	}
	else
	{
		ssPath << L"WinCse-";
	}

	ssPath << L"abend-";
	ssPath << pid;
	ssPath << L'-';
	ssPath << tid;
	ssPath << L".log";

	std::ofstream ofs{ ssPath.str(), std::ios_base::app };

	//
	std::ostringstream ssCause;

	ssCause << std::endl;
	ssCause << "cause; ";
	ssCause << file;
	ssCause << "(";
	ssCause << line;
	ssCause << "); signum(";
	ssCause << signum;
	ssCause << "); ";
	ssCause << func;
	ssCause << std::endl;
	ssCause << "LastError=";
	ssCause << ::GetLastError();
	ssCause << std::endl;
	ssCause << std::endl;

	const std::string causeStr{ ssCause.str() };

#ifdef _DEBUG
	::OutputDebugStringA(causeStr.c_str());
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

		std::ostringstream ss;
		ss << frames - i - 1;
		ss << ": ";
		ss << symbol->Name;
		ss << " - 0x";
		ss << symbol->Address;
		ss << std::endl;

		const std::string ss_str{ ss.str() };

#ifdef _DEBUG
		::OutputDebugStringA(ss_str.c_str());
#endif

		if (ofs)
		{
			ofs << ss_str;
		}
	}

	free(symbol);

	ofs.close();

	abort();
}

FatalError::FatalError(const std::string& argWhat, DWORD argLastError) noexcept
	:
	mWhat(argWhat), mNtstatus(FspNtStatusFromWin32(argLastError))
{
}

//
int NamedWorkersToMap(NamedWorker workers[], std::unordered_map<std::wstring, IWorker*>* pWorkerMap)
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

// ファイル名から FSP_FSCTL_DIR_INFO のヒープ領域を生成し、いくつかのメンバを設定して返却
DirInfoType makeEmptyDirInfo(const std::wstring& argFileName)
{
	APP_ASSERT(!argFileName.empty());

	const auto keyLen = argFileName.length();
	const auto keyLenBytes = keyLen * sizeof(WCHAR);
	const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
	const auto dirInfoSize = offFileNameBuf + keyLenBytes;
	const auto allocSize = dirInfoSize + sizeof(WCHAR);

	FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)calloc(1, allocSize);
	APP_ASSERT(dirInfo);

	dirInfo->Size = (UINT16)dirInfoSize;

	//dirInfo->FileInfo.IndexNumber = HashString(bucket.empty() ? key : bucket + L'/' + key);
	dirInfo->FileInfo.IndexNumber = HashString(argFileName);

	//
	// 実行時にエラーとなる (Buffer is too small)
	// 
	// おそらく、FSP_FSCTL_DIR_INFO.FileNameBuf は [] として定義されているため
	// wcscpy_s では 0 byte 領域へのバッファ・オーバーフローとして認識されて
	// しまうのではないかと思う
	// 
	//wcscpy_s(dirInfo->FileNameBuf, wkeyLen, wkey.c_str());

	memcpy(dirInfo->FileNameBuf, argFileName.c_str(), keyLenBytes);

	return std::make_shared<DirInfoView>(dirInfo);
}

DirInfoType makeDirInfo(const std::wstring& argFileName, UINT64 argFileTime, UINT32 argFileAttributes)
{
	APP_ASSERT(!argFileName.empty());

	auto dirInfo = makeEmptyDirInfo(argFileName);
	APP_ASSERT(dirInfo);

	UINT32 fileAttributes = argFileAttributes;

	if (argFileName != L"." && argFileName != L".." && argFileName.at(0) == L'.')
	{
		// 隠しファイル

		fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
	}

	dirInfo->FileInfo.FileAttributes = fileAttributes;

	dirInfo->FileInfo.CreationTime = argFileTime;
	dirInfo->FileInfo.LastAccessTime = argFileTime;
	dirInfo->FileInfo.LastWriteTime = argFileTime;
	dirInfo->FileInfo.ChangeTime = argFileTime;

	return dirInfo;
}

NTSTATUS HandleToSecurityInfo(HANDLE Handle,
	PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize /* nullable */)
{
	DWORD SecurityDescriptorSizeNeeded = 0;

	if (0 != PSecurityDescriptorSize)
	{
		if (!::GetKernelObjectSecurity(Handle,
			OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			SecurityDescriptor, (DWORD)*PSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
		{
			*PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
			return FspNtStatusFromWin32(::GetLastError());
		}

		*PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
	}

	return STATUS_SUCCESS;
}

// argKey                       parentDir       filename
// ------------------------------------------------------
// ""                      NG
// "dir"                   OK   ""              "dir"       
// "dir/"                  OK   ""              "dir"
// "dir/key.txt"           OK   "dir/"          "key.txt"
// "dir/key.txt/"          OK   "dir/"          "key.txt/"
// "dir/subdir/key.txt"    OK   "dir/subdir/"   "key.txt"
// "dir/subdir/key.txt/"   OK   "dir/subdir/"   "key.txt/"

bool SplitPath(const std::wstring& argKey, std::wstring* pParentDir /* nullable */, std::wstring* pFileName /* nullable */)
{
	// キーから親ディレクトリを取得

	auto tokens{ SplitString(argKey, L'/', false) };
	if (tokens.empty())
	{
		return false;
	}

	auto fileName{ tokens.back() };
	if (fileName.empty())
	{
		return false;
	}

	tokens.pop_back();

	// 検索対象の親ディレクトリ

	auto parentDir{ JoinStrings(tokens, L'/', false) };
	if (parentDir.empty())
	{
		// バケットのルート・ディレクトリから検索

		// "" --> ""
	}
	else
	{
		// サブディレクトリから検索

		// "dir"        --> "dir/"
		// "dir/subdir" --> "dir/subdir/"

		parentDir += L'/';
	}

	// 検索対象のファイル名 (ディレクトリ名)

	if (argKey.back() == L'/')
	{
		// SplitString() で "/" が除かれてしまうので、argKey に "dir/" や "dir/file.txt/"
		// が指定されているときは filename に "/" を付与

		fileName += L'/';
	}

	if (pParentDir)
	{
		*pParentDir = std::move(parentDir);
	}

	if (pFileName)
	{
		*pFileName = std::move(fileName);
	}

	return true;
}

int GetIniIntW(const std::wstring& confPath, PCWSTR argSection, PCWSTR keyName, int defaultValue, int minValue, int maxValue)
{
	LastErrorBackup _backup;

	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	int ret = ::GetPrivateProfileIntW(argSection, keyName, defaultValue, confPath.c_str());
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

#define INI_LINE_BUFSIZ		(1024)

bool GetIniStringW(const std::wstring& confPath, PCWSTR argSection, PCWSTR keyName, std::wstring* pValue)
{
	LastErrorBackup _backup;

	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	std::vector<WCHAR> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringW(argSection, keyName, L"", buf.data(), (DWORD)buf.size(), confPath.c_str());
	const auto lerr = ::GetLastError();

	if (lerr != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::wstring(buf.data());

	return true;
}

bool GetIniStringA(const std::string& confPath, PCSTR argSection, PCSTR keyName, std::string* pValue)
{
	LastErrorBackup _backup;

	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	std::vector<char> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringA(argSection, keyName, "", buf.data(), (DWORD)buf.size(), confPath.c_str());

	if (::GetLastError() != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::string(buf.data());

	return true;
}

//
// ObjectKey
//
void ObjectKey::reset() noexcept
{
	mHasBucket = !mBucket.empty();
	mHasKey = !mKey.empty();
	mBucketKey = mBucket + L'/' + mKey;

	//
	// キーが空 (!mHasKey) --> bucket			== ディレクトリ
	// 空でない (mHasKey)  --> bucket/key		== ファイル
	//                         bucket/key/		== ディレクトリ
	//
	mMeansDir = mHasBucket ? (!mHasKey || (mHasKey && mKey.back() == L'/')) : false;
	mMeansFile = mHasBucket ? (mHasKey && mKey.back() != L'/') : false;
}

ObjectKey ObjectKey::fromPath(const std::wstring& argPath)
{
	// パス文字列をバケット名とキーに分割

	APP_ASSERT(!argPath.empty());
	APP_ASSERT(argPath.at(0) != L'/');			// バケットの先頭は "/" ではないはず

	std::wstring bucket;
	std::wstring key;

	std::vector<std::wstring> tokens;

	std::wistringstream input{ argPath };
	std::wstring token;

	while (std::getline(input, token, L'/'))
	{
		tokens.emplace_back(std::move(token));
	}

	switch (tokens.size())
	{
		case 0:
		{
			break;
		}
		case 1:
		{
			bucket = std::move(tokens[0]);

			break;
		}
		default:
		{
			bucket = std::move(tokens[0]);

			std::wostringstream ss;
			for (int i = 1; i < tokens.size(); ++i)
			{
				if (i != 1)
				{
					ss << L'/';
				}
				ss << tokens[i];
			}
			key = ss.str();

			APP_ASSERT(!key.empty());

			if (argPath.back() == L'/')
			{
				// "/" で分割しているので、入力の一番最後にある "/" も消えてしまう
				// この場合を考慮して、キーの最後に "/" を追加

				key += L'/';
			}

			break;
		}
	}

	return ObjectKey(bucket, key);
}

ObjectKey ObjectKey::fromWinPath(const std::wstring& argWinPath)
{
	// Windows パス文字列をバケット名とキーに分割

	APP_ASSERT(!argWinPath.empty());
	APP_ASSERT(argWinPath.at(0) == L'\\');

	std::wstring bucket;
	std::wstring key;

	std::vector<std::wstring> tokens;

	std::wistringstream input{ argWinPath };
	std::wstring token;

	while (std::getline(input, token, L'\\'))
	{
		tokens.emplace_back(std::move(token));
	}

	switch (tokens.size())
	{
		case 0:			// "\bucket" となり、"\" の左側が 0 なので常に "" となるはず
		case 1:
		{
			break;
		}
		case 2:
		{
			bucket = std::move(tokens[1]);
			// mKey is empty

			break;
		}
		default:
		{
			bucket = std::move(tokens[1]);

			std::wostringstream ss;
			for (int i = 2; i < tokens.size(); ++i)
			{
				if (i != 2)
				{
					ss << L'/';
				}
				ss << tokens[i];
			}
			key = ss.str();

			APP_ASSERT(!key.empty());

			// "\\" で分割するため、引数の最後が "\\" でもなくってしまう
			// ので、ここで補填する

			if (argWinPath.back() == L'\\')
			{
				key += L'/';
			}

			break;
		}
	}

	return ObjectKey(bucket, key);
}

//
// std::map のキーにする場合に必要
//
bool ObjectKey::operator<(const ObjectKey& other) const noexcept
{
	if (mBucket < other.mBucket) {			// bucket
		return true;
	}
	else if (mBucket > other.mBucket) {
		return false;
	}
	else if (mKey < other.mKey) {			// key
		return true;
	}
	else if (mKey > other.mKey) {
		return false;
	}

	return false;
}

bool ObjectKey::operator>(const ObjectKey& other) const noexcept
{
	if (this->operator<(other))
	{
		return false;
	}
	else if (this->operator==(other))
	{
		return false;
	}

	return true;
}

ObjectKey ObjectKey::toFile() const noexcept
{
	//
	// キーの後ろに "/" を付与したものを返却する
	// キーがない場合は自分のコピーを返す			--> バケット名のみの場合
	// そもそもディレクトリの場合もコピーを返す
	//

	if (mHasBucket && mHasKey)
	{
		if (mMeansDir)
		{
			return ObjectKey{ mBucket, mKey.substr(0, mKey.size() - 1) };
		}
	}

	return *this;
}

ObjectKey ObjectKey::toDir() const noexcept
{
	//
	// キーの後ろに "/" を付与したものを返却する
	// キーがない場合は自分のコピーを返す			--> バケット名のみの場合
	// そもそもディレクトリの場合もコピーを返す
	//

	if (mHasBucket && mHasKey)
	{
		if (mMeansFile)
		{
			return ObjectKey{ mBucket, mKey + L'/' };
		}
	}

	return *this;
}

std::unique_ptr<ObjectKey> ObjectKey::toParentDir() const
{
	//
	// キーがあった場合は "/" で分割した親ディレクトリを返す
	//

	if (mHasBucket && mHasKey)
	{
		std::wstring parentDir;
		if (SplitPath(mKey, &parentDir, nullptr))
		{
			return std::make_unique<ObjectKey>(mBucket, parentDir);
		}
	}

	return nullptr;
}

ObjectKey ObjectKey::append(const std::wstring& arg) const noexcept
{
	return ObjectKey{ mBucket, mKey + arg };
}

std::string ObjectKey::bucketA() const
{
	return WC2MB(mBucket);
}

std::string ObjectKey::keyA() const
{
	return WC2MB(mKey);
}

std::string ObjectKey::strA() const
{
	return WC2MB(mBucketKey);
}

//
// CSDeviceContext
//
CSDeviceContext::CSDeviceContext(const std::wstring& argCacheDataDir,
	const WCSE::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo) noexcept
	:
	mCacheDataDir(argCacheDataDir),
	mFileInfo(argFileInfo),
	mObjKey(FA_IS_DIRECTORY(argFileInfo.FileAttributes) ? argObjKey.toDir() : argObjKey)
{
}

bool CSDeviceContext::isDir() const noexcept
{
	return FA_IS_DIRECTORY(mFileInfo.FileAttributes);
}

std::wstring CSDeviceContext::getCacheFilePath() const
{
	return GetCacheFilePath(mCacheDataDir, mObjKey.str());
}

//
// FileHandle
//
BOOL FileHandle::setFileTime(const FSP_FSCTL_FILE_INFO& fileInfo)
{
	return setFileTime(fileInfo.CreationTime, fileInfo.LastWriteTime);
}

BOOL FileHandle::setFileTime(UINT64 argCreationTime, UINT64 argLastWriteTime)
{
	APP_ASSERT(valid());

	FILETIME ftCreation;
	WinFileTime100nsToWinFile(argCreationTime, &ftCreation);

	FILETIME ftLastWrite;
	WinFileTime100nsToWinFile(argLastWriteTime, &ftLastWrite);

	FILETIME ftNow;
	::GetSystemTimeAsFileTime(&ftNow);

	return ::SetFileTime(mHandle, &ftCreation, &ftNow, &ftLastWrite);
}

/*
BOOL FileHandle::setBasicInfo(const FSP_FSCTL_FILE_INFO& fileInfo)
{
	return setBasicInfo(fileInfo.FileAttributes, fileInfo.CreationTime, fileInfo.LastWriteTime);
}

BOOL FileHandle::setBasicInfo(UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastWriteTime)
{
	APP_ASSERT(valid());

	UINT32 FileAttributes = argFileAttributes;

	if (INVALID_FILE_ATTRIBUTES == FileAttributes)
		FileAttributes = 0;
	else if (0 == FileAttributes)
		FileAttributes = FILE_ATTRIBUTE_NORMAL;

	FILE_BASIC_INFO BasicInfo = {};

	BasicInfo.FileAttributes = FileAttributes;
	BasicInfo.CreationTime.QuadPart = argCreationTime;
	BasicInfo.LastAccessTime.QuadPart = GetCurrentWinFileTime100ns();
	BasicInfo.LastWriteTime.QuadPart = argLastWriteTime;
	//BasicInfo.ChangeTime = argChangeTime;

	return ::SetFileInformationByHandle(mHandle,
		FileBasicInfo, &BasicInfo, sizeof BasicInfo);
}

LONGLONG FileHandle::getFileSize()
{
	APP_ASSERT(valid());

	LARGE_INTEGER fileSize;

	if (!::GetFileSizeEx(mHandle, &fileSize))
	{
		return -1LL;
	}

	return fileSize.QuadPart;
}
*/

//
// LogBlock
//
static thread_local int gDepth = 0;

LogBlock::LogBlock(PCWSTR argFile, int argLine, PCWSTR argFunc) noexcept
	:
	mFile(argFile), mLine(argLine), mFunc(argFunc)
{
	GetLogger()->traceW_impl(gDepth, mFile, mLine, mFunc, L"{enter}");
	gDepth++;
}

LogBlock::~LogBlock()
{
	gDepth--;
	GetLogger()->traceW_impl(gDepth, mFile, -1, mFunc, L"{leave}");
}

int LogBlock::depth() const noexcept
{
	return gDepth;
}

} // namespace WCSE

// EOF
