#pragma once

#ifndef _RELEASE
#ifndef _DEBUG
#define _RELEASE				(1)
#endif
#endif

//
// オリジナルの "passthrough.c" と同じ動きをさせるためのスイッチ
//
#define WINFSP_PASSTHROUGH		(0)

//
// 関数のエクスポート
//
#ifdef WINCSELIB_EXPORTS
#define WINCSELIB_API __declspec(dllexport)
#else
#define WINCSELIB_API __declspec(dllimport)
#endif

//
// メモリリーク検出にソースコード名を出力するためには、このファイルを
// 一番最初に include しなければならない
//
#include "internal_define_alloc.h"

//
// "ntstatus.h" を include するためには以下の記述 (define/undef) が必要だが
// 同じことが "winfsp/winfsp.h" で行われているのでコメント化
// 
//#define WIN32_NO_STATUS
//#include <windows.h>
//#undef WIN32_NO_STATUS

#pragma warning(push, 0)
#include <winfsp/winfsp.h>
#pragma warning(pop)

//
// passthrough.c に定義されていたもののうち、アプリケーションに
// 必要となるものを外だし
//
#define ALLOCATION_UNIT         (4096)

typedef struct
{
#if WINFSP_PASSTHROUGH
	HANDLE Handle;

#endif
	PVOID DirBuffer;

	// 追加情報
	PWSTR FileName;
	FSP_FSCTL_FILE_INFO FileInfo;
	PVOID UParam;
}
PTFS_FILE_CONTEXT;

#include <string>
#include <list>
#include <vector>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <functional>

//
// インターフェース定義で使うので、ここで define
//
#define CALLER_ARG0				[[maybe_unused]] const std::wstring& caller_
#define CALLER_ARG				CALLER_ARG0,

#define START_CALLER0			std::wstring(__FUNCTIONW__)
#define START_CALLER			START_CALLER0,

#define CALL_FROM()				(caller_)
#define CALL_CHAIN()			(CALL_FROM() + L"->" + __FUNCTIONW__)
#define CONT_CALLER0			CALL_CHAIN()
#define CONT_CALLER				CONT_CALLER0,

namespace WCSE {

// HANDLE 用 RAII

template<HANDLE InvalidHandleValue>
class HandleRAII
{
protected:
	HANDLE mHandle;

public:
	HandleRAII() noexcept : mHandle(InvalidHandleValue) { }

	HandleRAII(HANDLE argHandle) noexcept : mHandle(argHandle) { }

	HandleRAII(HandleRAII& other) noexcept = delete;

	HandleRAII(HandleRAII&& other) noexcept : mHandle(other.mHandle)
	{
		other.mHandle = InvalidHandleValue;
	}

	HandleRAII& operator=(HandleRAII& other) noexcept = delete;

	HandleRAII& operator=(HandleRAII&& other) noexcept
	{
		if (this != &other)
		{
			close();
			mHandle = other.mHandle;
			other.mHandle = InvalidHandleValue;
		}

		return *this;
	}

	HANDLE handle() const noexcept { return mHandle; }
	bool invalid() const noexcept { return mHandle == InvalidHandleValue; }
	bool valid() const noexcept { return !invalid(); }

	void close() noexcept
	{
		if (mHandle != InvalidHandleValue)
		{
			::CloseHandle(mHandle);
			mHandle = InvalidHandleValue;
		}
	}

	virtual ~HandleRAII() noexcept { close(); }
};

class FileHandle : public HandleRAII<INVALID_HANDLE_VALUE>
{
public:
	using HandleRAII::HandleRAII;

	WINCSELIB_API BOOL setFileTime(const FSP_FSCTL_FILE_INFO& fileInfo);
	WINCSELIB_API BOOL setFileTime(UINT64 argCreationTime, UINT64 argLastWriteTime);
	//WINCSELIB_API BOOL setBasicInfo(const FSP_FSCTL_FILE_INFO& fileInfo);
	//WINCSELIB_API BOOL setBasicInfo(UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastWriteTime);
	//WINCSELIB_API LONGLONG getFileSize();
};

class EventHandle : public HandleRAII<(HANDLE)NULL>
{
public:
	using HandleRAII<(HANDLE)NULL>::HandleRAII;
};

//
// FSP_FSCTL_DIR_INFO に付与したい項目があるが、WinFsp のリソースであり
// 直接拡張するわけにはいかないので、内部に持たせ View として機能させる
//
class DirInfoView
{
private:
	FSP_FSCTL_DIR_INFO* const mDirInfo;

public:
	FSP_FSCTL_FILE_INFO& FileInfo;
	const WCHAR* const FileNameBuf;
	std::unordered_map<std::wstring, std::wstring> mUserProperties;

	explicit DirInfoView(FSP_FSCTL_DIR_INFO* argDirInfo) noexcept
		:
		mDirInfo(argDirInfo),
		FileInfo(mDirInfo->FileInfo),
		FileNameBuf(mDirInfo->FileNameBuf)
	{
	}

	FSP_FSCTL_DIR_INFO* data() const noexcept 
	{
		return mDirInfo;
	}

	~DirInfoView() noexcept
	{
		free(mDirInfo);
	}
};

using DirInfoType = std::shared_ptr<DirInfoView>;
using DirInfoListType = std::list<DirInfoType>;

}

// インターフェース定義
#include "ICSService.hpp"
#include "ICSDriver.hpp"
#include "ICSDevice.hpp"
#include "ILogger.hpp"
#include "IWorker.hpp"

// 以降はアプリケーション関連

namespace WCSE {

//
// グローバル関数
//

WINCSELIB_API BOOL DeleteFilePassively(PCWSTR argPath);
WINCSELIB_API std::wstring GetCacheFilePath(const std::wstring& argDir, const std::wstring& argName);
WINCSELIB_API NTSTATUS PathToFileInfo(const std::wstring& path, FSP_FSCTL_FILE_INFO* pFileInfo);
WINCSELIB_API bool MkdirIfNotExists(const std::wstring& dir);
WINCSELIB_API bool forEachFiles(const std::wstring& argDir, const std::function<void(const WIN32_FIND_DATA& wfd, const std::wstring& fullPath)>& callback);
WINCSELIB_API bool forEachDirs(const std::wstring& argDir, const std::function<void(const WIN32_FIND_DATA& wfd, const std::wstring& fullPath)>& callback);

WINCSELIB_API bool Base64EncodeA(const std::string& src, std::string* pDst);
WINCSELIB_API bool Base64DecodeA(const std::string& src, std::string* pDst);

WINCSELIB_API UINT64 UtcMillisToWinFileTime100ns(UINT64 argUtcMillis);
WINCSELIB_API UINT64 WinFileTime100nsToUtcMillis(UINT64 fileTime100ns);

WINCSELIB_API UINT64 WinFileTimeToWinFileTime100ns(const FILETIME& ft);
WINCSELIB_API void WinFileTime100nsToWinFile(UINT64 ft100ns, FILETIME* pFileTime);

WINCSELIB_API void UtcMillisToWinFileTime(UINT64 argUtcMillis, FILETIME* pFileTime);
WINCSELIB_API UINT64 WinFileTimeToUtcMillis(const FILETIME &ft);
WINCSELIB_API bool PathToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite);
WINCSELIB_API UINT64 GetCurrentUtcMillis();
WINCSELIB_API UINT64 GetCurrentWinFileTime100ns();

WINCSELIB_API long long int TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API long long int TimePointToUtcSecs(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp);

WINCSELIB_API std::wstring UtcMilliToLocalTimeStringW(UINT64 milliseconds);
WINCSELIB_API std::wstring WinFileTime100nsToLocalTimeStringW(UINT64 fileTime100ns);
WINCSELIB_API std::string WinFileTime100nsToLocalTimeStringA(UINT64 ft100ns);
WINCSELIB_API std::wstring WinFileTimeToLocalTimeStringW(const FILETIME &ft);

WINCSELIB_API UINT64 STCTimeToUTCMilliSecW(const std::wstring& path);
WINCSELIB_API UINT64 STCTimeToWinFileTimeW(const std::wstring& path);
WINCSELIB_API UINT64 STCTimeToUTCMilliSecA(const std::string& path);
WINCSELIB_API UINT64 STCTimeToWinFileTimeA(const std::string& path);

WINCSELIB_API std::wstring MB2WC(const std::string& str);
WINCSELIB_API std::string WC2MB(const std::wstring& wstr);

WINCSELIB_API std::wstring TrimW(const std::wstring& str);
WINCSELIB_API std::string TrimA(const std::string& str);

WINCSELIB_API std::wstring WildcardToRegexW(const std::wstring& wildcard);
WINCSELIB_API std::string WildcardToRegexA(const std::string& wildcard);

WINCSELIB_API std::vector<std::wstring> SplitString(const std::wstring& input, wchar_t sep, bool ignoreEmpty);
WINCSELIB_API std::wstring ToUpper(const std::wstring& input);

WINCSELIB_API int GetIniIntW(const std::wstring& confPath, PCWSTR argSection, PCWSTR keyName, int defaultValue, int minValue, int maxValue);
WINCSELIB_API bool GetIniStringW(const std::wstring& confPath, PCWSTR argSection, PCWSTR keyName, std::wstring* pValue);
WINCSELIB_API bool GetIniStringA(const std::string& confPath, PCSTR argSection, PCSTR keyName, std::string* pValue);

WINCSELIB_API size_t HashString(const std::wstring& str);

WINCSELIB_API bool DecryptAES(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& encrypted, std::vector<BYTE>* pDecrypted);
WINCSELIB_API LSTATUS GetCryptKeyFromRegistryA(std::string* pOutput);
WINCSELIB_API LSTATUS GetCryptKeyFromRegistryW(std::wstring* pOutput);
WINCSELIB_API NTSTATUS ComputeSHA256A(const std::string& input, std::string* pOutput);
WINCSELIB_API NTSTATUS ComputeSHA256W(const std::wstring& input, std::wstring* pOutput);

WINCSELIB_API void AbnormalEnd(PCSTR file, int line, PCSTR func, int signum);
WINCSELIB_API int NamedWorkersToMap(NamedWorker workers[], std::unordered_map<std::wstring, IWorker*>* pWorkerMap);

WINCSELIB_API NTSTATUS HandleToSecurityInfo(HANDLE Handle,
	PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize /* nullable */);

WINCSELIB_API bool CreateLogger(PCWSTR argTempDir, PCWSTR argTrcDir, PCWSTR argDllType);
WINCSELIB_API ILogger* GetLogger();
WINCSELIB_API void DeleteLogger();

// ファイル名から FSP_FSCTL_DIR_INFO のヒープ領域を生成し、いくつかのメンバを設定して返却
WINCSELIB_API DirInfoType makeDirInfo(const std::wstring& argFileName);

WINCSELIB_API bool SplitPath(const std::wstring& argKey,
    std::wstring* pParentDir /* nullable */, std::wstring* pFileName /* nullable */);

//
// ログ・ブロックの情報
//
class WINCSELIB_API LogBlock
{
	PCWSTR mFile;
	const int mLine;
	PCWSTR mFunc;

public:
	LogBlock(PCWSTR argFile, int argLine, PCWSTR argFunc) noexcept;
	~LogBlock();

	int depth() const noexcept;
	static int getCount() noexcept;
};

//
// GetLastError() 値の保存
//
class LastErrorBackup
{
	const DWORD mLastError;

public:
	LastErrorBackup() noexcept
		:
		mLastError(::GetLastError())
	{
	}

	~LastErrorBackup()
	{
		::SetLastError(mLastError);
	}
};

struct FatalError : public std::exception
{
	const std::string mWhat;
	const NTSTATUS mNtstatus;

	WINCSELIB_API FatalError(const std::string& argWhat, DWORD argLastError) noexcept;

	FatalError(const std::string& argWhat, NTSTATUS argNtstatus) noexcept
		:
		mWhat(argWhat), mNtstatus(argNtstatus)
	{
	}

	FatalError(const std::string& argWhat) noexcept
		: mWhat(argWhat), mNtstatus(STATUS_UNSUCCESSFUL)
	{
	}

	const char* what() const noexcept override
	{
		return mWhat.c_str();
	}
};

template <typename T>
std::string getDerivedClassNamesA(T* baseClass)
{
	const std::type_info& typeInfo = typeid(*baseClass);
	return typeInfo.name();
}

template <typename T>
std::wstring getDerivedClassNamesW(T* baseClass)
{
	const std::type_info& typeInfo = typeid(*baseClass);
	return MB2WC(typeInfo.name());
}

template <typename ContainerT, typename SeparatorT>
std::wstring JoinStrings(const ContainerT& tokens, SeparatorT sep, bool ignoreEmpty)
{
	std::wostringstream ss;

	bool first = true;
	for (const auto& token: tokens)
	{
		if (ignoreEmpty)
		{
			if (token.empty())
			{
				continue;
			}
		}

		if (first)
		{
			first = false;
		}
		else
		{
			ss << sep;
		}

		ss << token;
	}

	return ss.str();
}

// ファイルサイズ

constexpr INT64 FILESIZE_1B = 1LL;
constexpr UINT64 FILESIZE_1Bu = 1ULL;

constexpr INT64 FILESIZE_1KiB = FILESIZE_1B * 1024LL;
constexpr UINT64 FILESIZE_1KiBu = FILESIZE_1Bu * 1024ULL;

constexpr INT64 FILESIZE_1MiB = FILESIZE_1KiB * 1024LL;
constexpr UINT64 FILESIZE_1MiBu = FILESIZE_1KiBu * 1024ULL;

constexpr INT64 FILESIZE_1GiB = FILESIZE_1MiB * 1024LL;
constexpr UINT64 FILESIZE_1GiBu = FILESIZE_1MiBu * 1024ULL;

// 時間 (DWORD)

constexpr int32_t TIMEMILLIS_1SEC = 1000L;
constexpr uint32_t TIMEMILLIS_1SECu = 1000UL;

constexpr int32_t TIMEMILLIS_1MIN = TIMEMILLIS_1SEC * 60;
constexpr uint32_t TIMEMILLIS_1MINu = TIMEMILLIS_1SECu * 60;

constexpr int32_t TIMEMILLIS_1HOUR = TIMEMILLIS_1MIN * 60;
constexpr uint32_t TIMEMILLIS_1HOURu = TIMEMILLIS_1MINu * 60;

constexpr int32_t TIMEMILLIS_1DAY = TIMEMILLIS_1HOUR * 24;
constexpr uint32_t TIMEMILLIS_1DAYu = TIMEMILLIS_1HOURu * 60;

// 時間 (INT64, UINT64)

constexpr INT64 TIMEMILLIS_1SECll = 1000LL;
constexpr UINT64 TIMEMILLIS_1SECull = 1000ULL;

constexpr INT64 TIMEMILLIS_1MINll = TIMEMILLIS_1SECll * 60;
constexpr UINT64 TIMEMILLIS_1MINull = TIMEMILLIS_1SECull * 60;

constexpr INT64 TIMEMILLIS_1HOURll = TIMEMILLIS_1MINll * 60;
constexpr UINT64 TIMEMILLIS_1HOURull = TIMEMILLIS_1MINull * 60;

constexpr INT64 TIMEMILLIS_1DAYll = TIMEMILLIS_1HOURll * 24;
constexpr UINT64 TIMEMILLIS_1DAYull = TIMEMILLIS_1HOURull * 60;


} // namespace WCSE

typedef struct
{
    long GetFileInfoInternal;
    long GetVolumeInfo;
    long SetVolumeLabel_;
    long GetSecurityByName;
    long Create;
    long Open;
    long Overwrite;
    long Cleanup;
    long Close;
    long Read;
    long Write;
    long Flush;
    long GetFileInfo;
    long SetBasicInfo;
    long SetFileSize;
    long Rename;
    long GetSecurity;
    long SetSecurity;
    long ReadDirectory;
    long SetDelete;
}
WINFSP_STATS;

typedef struct
{
	WCSE::ICSDriver* pCSDriver;
	WINFSP_STATS FspStats;
}
WINCSE_IF;

extern "C"
{
	WINCSELIB_API NTSTATUS GetFileInfoInternal(HANDLE Handle, FSP_FSCTL_FILE_INFO* FileInfo);
	WINCSELIB_API int WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WINCSE_IF* appif);
}

// -----------------------------
//
// マクロ定義
//
#define NEW_LOG_BLOCK() \
	WCSE::LogBlock logBlock_(__FILEW__, __LINE__, __FUNCTIONW__)

#define LOG_BLOCK()		logBlock_
#define LOG_DEPTH()		LOG_BLOCK().depth()

#define traceA(format, ...) \
	WCSE::GetLogger()->traceA_impl(LOG_DEPTH(), __FILE__, __LINE__, __FUNCTION__, format, __VA_ARGS__)

#define traceW(format, ...) \
	WCSE::GetLogger()->traceW_impl(LOG_DEPTH(), __FILEW__, __LINE__, __FUNCTIONW__, format, __VA_ARGS__)

#define APP_ASSERT(expr) \
    if (!(expr)) { \
        WCSE::AbnormalEnd(__FILE__, __LINE__, __FUNCTION__, -1); \
    }

#define FA_IS_DIRECTORY(fa)		((fa) & FILE_ATTRIBUTE_DIRECTORY)

#define FA_MEANS_TEMPORARY(fa)	((fa) & FILE_ATTRIBUTE_HIDDEN || (fa) & FILE_ATTRIBUTE_TEMPORARY || (fa) & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)

#define BOOL_CSTRW(b)			((b) ? L"true" : L"false")
#define BOOL_CSTRA(b)			((b) ? "true" : "false")

// EOF
