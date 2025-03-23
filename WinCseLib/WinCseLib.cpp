#include "WinCseLib.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <dbghelp.h>

#pragma comment(lib, "Crypt32.lib")             // CryptBinaryToStringA
#pragma comment(lib, "Dbghelp.lib")             // SymInitialize


namespace WinCseLib {

void AbnormalEnd(const char* file, const int line, const char* func, const int signum)
{
	wchar_t szTempPath[MAX_PATH];
	::GetTempPathW(MAX_PATH, szTempPath);
	std::wstring tempPath{ szTempPath };

	const DWORD pid = ::GetCurrentProcessId();
	const DWORD tid = ::GetCurrentThreadId();

	std::wstringstream ssPath;
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
	std::stringstream ssCause;
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

	::OutputDebugStringA(causeStr.c_str());

	if (ofs)
	{
		ofs << causeStr;
	}

	//
	const int maxFrames = 62;
	void* stack[maxFrames];
	HANDLE process = ::GetCurrentProcess();
	::SymInitialize(process, NULL, TRUE);

	USHORT frames = ::CaptureStackBackTrace(0, maxFrames, stack, NULL);
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (USHORT i = 0; i < frames; i++)
	{
		::SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);

		std::stringstream ss;
		ss << frames - i - 1;
		ss << ": ";
		ss << symbol->Name;
		ss << " - 0x";
		ss << symbol->Address;
		ss << std::endl;

		const std::string ss_str{ ss.str() };

		::OutputDebugStringA(ss_str.c_str());

		if (ofs)
		{
			ofs << ss_str;
		}
	}

	free(symbol);

	ofs.close();

	abort();
}

// malloc, calloc で確保したメモリを shared_ptr で解放するための関数
template <typename T>
void free_deleter(T* ptr)
{
	free(ptr);
}

// ファイル名から FSP_FSCTL_DIR_INFO のヒープ領域を生成し、いくつかのメンバを設定して返却
DirInfoType makeDirInfo(const ObjectKey& argObjKey)
{
	const auto& key{ argObjKey.key() };

	APP_ASSERT(!key.empty());		// バケット名をキーに入れて利用することもあるので、hasKey では判定できない

	const auto keyLen = key.length();
	const auto keyLenBytes = keyLen * sizeof(WCHAR);
	const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
	const auto dirInfoSize = offFileNameBuf + keyLenBytes;
	const auto allocSize = dirInfoSize + sizeof(WCHAR);

	FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)calloc(1, allocSize);
	APP_ASSERT(dirInfo);

	dirInfo->Size = (UINT16)dirInfoSize;

	//dirInfo->FileInfo.IndexNumber = HashString(bucket.empty() ? key : bucket + L'/' + key);
	dirInfo->FileInfo.IndexNumber = HashString(argObjKey.str());

	//
	// 実行時にエラーとなる (Buffer is too small)
	// 
	// おそらく、FSP_FSCTL_DIR_INFO.FileNameBuf は [] として定義されているため
	// wcscpy_s では 0 byte 領域へのバッファ・オーバーフローとして認識されて
	// しまうのではないかと思う
	// 
	//wcscpy_s(dirInfo->FileNameBuf, wkeyLen, wkey.c_str());

	memcpy(dirInfo->FileNameBuf, key.c_str(), keyLenBytes);

	return DirInfoType(dirInfo, free_deleter<FSP_FSCTL_DIR_INFO>);
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

bool SplitPath(const std::wstring& argKey, std::wstring* pParentDir /* nullable */, std::wstring* pFilename /* nullable */)
{
	// キーから親ディレクトリを取得

	auto tokens{ SplitString(argKey, L'/', false) };
	if (tokens.empty())
	{
		return false;
	}

	auto filename{ tokens.back() };
	if (filename.empty())
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

		filename += L'/';
	}

	if (pParentDir)
	{
		*pParentDir = std::move(parentDir);
	}

	if (pFilename)
	{
		*pFilename = std::move(filename);
	}

	return true;
}

#define INI_LINE_BUFSIZ		(1024)

bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue)
{
	APP_ASSERT(argSection);
	APP_ASSERT(argSection[0]);

	std::vector<wchar_t> buf(INI_LINE_BUFSIZ);

	::SetLastError(ERROR_SUCCESS);
	::GetPrivateProfileStringW(argSection, keyName, L"", buf.data(), (DWORD)buf.size(), confPath.c_str());

	if (::GetLastError() != ERROR_SUCCESS)
	{
		return false;
	}

	*pValue = std::wstring(buf.data());

	return true;
}

bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue)
{
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

ObjectKey::ObjectKey(const std::wstring& argWinPath)
{
	// Windows パス文字列をバケット名とキーに分割

	std::wstringstream input{ argWinPath };

	std::vector<std::wstring> tokens;
	std::wstring token;

	while (std::getline(input, token, L'\\'))
	{
		tokens.emplace_back(std::move(token));
	}

	switch (tokens.size())
	{
		case 0:
		case 1:
		{
			// mBucket is empty
			// mKey is empty

			break;
		}
		case 2:
		{
			mBucket = std::move(tokens[1]);
			// mKey is empty

			break;
		}
		default:
		{
			mBucket = std::move(tokens[1]);

			std::wstringstream output;
			for (int i = 2; i < tokens.size(); ++i)
			{
				if (i != 2)
				{
					output << L'/';
				}
				output << std::move(tokens[i]);
			}
			mKey = output.str();

			break;
		}
	}

	if (!mKey.empty())
	{
		// "\\" で分割するため、引数の最後が "\\" でもなくってしまう
		// ので、ここで補填する

		if (argWinPath.back() == L'\\')
		{
			mKey += L'/';
		}
	}

	reset();
}

//
// std::map のキーにする場合に必要
//
bool ObjectKey::operator<(const ObjectKey& other) const
{
	if (mBucket < other.mBucket) {			// bucket
		return true;
	}
	else if (mBucket > other.mBucket) {
		return false;
	}
	else if (mKey < other.mKey) {				// key
		return true;
	}
	else if (mKey > other.mKey) {
		return false;
	}

	return false;
}

bool ObjectKey::operator>(const ObjectKey& other) const
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

ObjectKey ObjectKey::toDir() const
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
CSDeviceContext::CSDeviceContext(
	const std::wstring& argCacheDataDir,
	const WinCseLib::ObjectKey& argObjKey,
	const FSP_FSCTL_FILE_INFO& argFileInfo)
	:
	mCacheDataDir(argCacheDataDir),
	mFileInfo(argFileInfo)
{
	if (FA_IS_DIR(mFileInfo.FileAttributes))
	{
		// ディレクトリの場合は '/' を付与する

		mObjKey = argObjKey.toDir();
	}
	else
	{
		mObjKey = argObjKey;
	}
}

bool CSDeviceContext::isDir() const
{
	return FA_IS_DIR(mFileInfo.FileAttributes);
}

std::wstring CSDeviceContext::getFilePathW() const
{
	return mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(getRemotePath());
}

std::string CSDeviceContext::getFilePathA() const
{
	return WC2MB(getFilePathW());
}

//
// FileHandle
//
bool FileHandle::setFileTime(UINT64 argCreationTime, UINT64 argLastWriteTime)
{
	APP_ASSERT(valid());

	FILETIME ftCreation;
	WinFileTime100nsToWinFile(argCreationTime, &ftCreation);

	FILETIME ftLastWrite;
	WinFileTime100nsToWinFile(argLastWriteTime, &ftLastWrite);

	FILETIME ftNow;
	::GetSystemTimeAsFileTime(&ftNow);

	if (!::SetFileTime(mHandle, &ftCreation, &ftNow, &ftLastWrite))
	{
		return false;
	}

	return true;
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

BOOL FileHandle::flushFileBuffers()
{
	APP_ASSERT(valid());

	return ::FlushFileBuffers(mHandle);
}

//
// LogBlock
//
static std::atomic<int> mCounter(0);
static thread_local int mDepth = 0;

LogBlock::LogBlock(const wchar_t* argFile, const int argLine, const wchar_t* argFunc)
	: mFile(argFile), mLine(argLine), mFunc(argFunc)
{
	mCounter++;

	GetLogger()->traceW_impl(mDepth, mFile, mLine, mFunc, L"{enter}");
	mDepth++;
}

LogBlock::~LogBlock()
{
	mDepth--;
	GetLogger()->traceW_impl(mDepth, mFile, -1, mFunc, L"{leave}");
}

int LogBlock::depth()
{
	return mDepth;
}

int LogBlock::getCount()
{
	return mCounter.load();
}

} // namespace WinCseLib

// EOF
