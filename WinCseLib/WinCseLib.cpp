#include "WinCseLib.h"
#include <fstream>
#include <filesystem>
#include <dbghelp.h>


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

// �t�@�C�������� FSP_FSCTL_DIR_INFO �̃q�[�v�̈�𐶐����A�������̃����o��ݒ肵�ĕԋp
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
	// ���s���ɃG���[�ƂȂ� (Buffer is too small)
	// 
	// �����炭�AFSP_FSCTL_DIR_INFO.FileNameBuf �� [] �Ƃ��Ē�`����Ă��邽��
	// wcscpy_s �ł� 0 byte �̈�ւ̃o�b�t�@�E�I�[�o�[�t���[�Ƃ��ĔF�������
	// ���܂��̂ł͂Ȃ����Ǝv��
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
		// �B���t�@�C��

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
	// �L�[����e�f�B���N�g�����擾

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

	// �����Ώۂ̐e�f�B���N�g��

	auto parentDir{ JoinStrings(tokens, L'/', false) };
	if (parentDir.empty())
	{
		// �o�P�b�g�̃��[�g�E�f�B���N�g�����猟��

		// "" --> ""
	}
	else
	{
		// �T�u�f�B���N�g�����猟��

		// "dir"        --> "dir/"
		// "dir/subdir" --> "dir/subdir/"

		parentDir += L'/';
	}

	// �����Ώۂ̃t�@�C���� (�f�B���N�g����)

	if (argKey.back() == L'/')
	{
		// SplitString() �� "/" ��������Ă��܂��̂ŁAargKey �� "dir/" �� "dir/file.txt/"
		// ���w�肳��Ă���Ƃ��� filename �� "/" ��t�^

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

int GetIniIntW(const std::wstring& confPath, const std::wstring& argSection, PCWSTR keyName, int defaultValue, int minValue, int maxValue)
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

bool GetIniBoolW(const std::wstring& confPath, const std::wstring& argSection, PCWSTR keyName, bool defaultValue)
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

bool GetIniStringW(const std::wstring& confPath, const std::wstring& argSection, PCWSTR keyName, std::wstring* pValue)
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
// ObjectKey
//
ObjectKey ObjectKey::fromPath(const std::wstring& argPath)
{
	// �p�X��������o�P�b�g���ƃL�[�ɕ���

	APP_ASSERT(!argPath.empty());
	APP_ASSERT(argPath.at(0) != L'/');			// �o�P�b�g�̐擪�� "/" �ł͂Ȃ��͂�

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
				// "/" �ŕ������Ă���̂ŁA���͂̈�ԍŌ�ɂ��� "/" �������Ă��܂�
				// ���̏ꍇ���l�����āA�L�[�̍Ō�� "/" ��ǉ�

				key += L'/';
			}

			break;
		}
	}

	return ObjectKey(bucket, key);
}

ObjectKey ObjectKey::fromWinPath(const std::wstring& argWinPath)
{
	// Windows �p�X��������o�P�b�g���ƃL�[�ɕ���

	if (argWinPath.empty() || argWinPath.at(0) != L'\\')
	{
		return ObjectKey();
	}

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
		case 0:			// "\bucket" �ƂȂ�A"\" �̍����� 0 �Ȃ̂ŏ�� "" �ƂȂ�͂�
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

			// "\\" �ŕ������邽�߁A�����̍Ōオ "\\" �ł��Ȃ����Ă��܂�
			// �̂ŁA�����ŕ�U����

			if (argWinPath.back() == L'\\')
			{
				key += L'/';
			}

			break;
		}
	}

	return ObjectKey(bucket, key);
}

std::optional<ObjectKey> ObjectKey::toParentDir() const
{
	//
	// �L�[���������ꍇ�� "/" �ŕ��������e�f�B���N�g����Ԃ�
	//

	if (mHasBucket && mHasKey)
	{
		std::wstring parentDir;
		if (SplitPath(mKey, &parentDir, nullptr))
		{
			return ObjectKey{ mBucket, parentDir };
		}
	}

	return std::nullopt;
}


std::string ObjectKey::bucketA() const { return WC2MB(mBucket); }
std::string ObjectKey::keyA() const { return WC2MB(mKey); }
std::string ObjectKey::strA() const { return WC2MB(mBucketKey); }

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
