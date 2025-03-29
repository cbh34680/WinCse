#pragma once

#ifndef _RELEASE
#ifndef _DEBUG
#define _RELEASE	(1)
#endif
#endif


#define SET_ATTRIBUTES_LOCAL_FILE		(0)
#define DELETE_ONLY_EMPTY_DIR           (0)


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
#include "WinFsp_c.h"

#include <string>
#include <list>
#include <vector>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <functional>

typedef std::shared_ptr<FSP_FSCTL_DIR_INFO> DirInfoType;
typedef std::list<DirInfoType> DirInfoListType;

#define CALLER_ARG0				const std::wstring& caller_
#define CALLER_ARG				CALLER_ARG0,

#define START_CALLER0			std::wstring(__FUNCTIONW__)
#define START_CALLER			START_CALLER0,

#define CALL_FROM()				(caller_)
#define CALL_CHAIN()			(CALL_FROM() + L"->" + __FUNCTIONW__)
#define CONT_CALLER0			CALL_CHAIN()
#define CONT_CALLER				CONT_CALLER0,


namespace WinCseLib {

// ファイルサイズ

constexpr int64_t FILESIZE_1B = 1LL;
constexpr uint64_t FILESIZE_1Bu = 1ULL;

constexpr int64_t FILESIZE_1KiB = FILESIZE_1B * 1024LL;
constexpr uint64_t FILESIZE_1KiBu = FILESIZE_1Bu * 1024ULL;

constexpr int64_t FILESIZE_1MiB = FILESIZE_1KiB * 1024LL;
constexpr uint64_t FILESIZE_1MiBu = FILESIZE_1KiBu * 1024ULL;

constexpr int64_t FILESIZE_1GiB = FILESIZE_1MiB * 1024LL;
constexpr uint64_t FILESIZE_1GiBu = FILESIZE_1MiBu * 1024ULL;

// 時間 (DWORD)

constexpr int32_t TIMEMILLIS_1SEC = 1000L;
constexpr uint32_t TIMEMILLIS_1SECu = 1000UL;

constexpr int32_t TIMEMILLIS_1MIN = TIMEMILLIS_1SEC * 60;
constexpr uint32_t TIMEMILLIS_1MINu = TIMEMILLIS_1SECu * 60;

constexpr int32_t TIMEMILLIS_1HOUR = TIMEMILLIS_1MIN * 60;
constexpr uint32_t TIMEMILLIS_1HOURu = TIMEMILLIS_1MINu * 60;

constexpr int32_t TIMEMILLIS_1DAY = TIMEMILLIS_1HOUR * 24;
constexpr uint32_t TIMEMILLIS_1DAYu = TIMEMILLIS_1HOURu * 60;

// 時間 (int64_t, uint64_t)

constexpr int64_t TIMEMILLIS_1SECll = 1000LL;
constexpr uint64_t TIMEMILLIS_1SECull = 1000ULL;

constexpr int64_t TIMEMILLIS_1MINll = TIMEMILLIS_1SECll * 60;
constexpr uint64_t TIMEMILLIS_1MINull = TIMEMILLIS_1SECull * 60;

constexpr int64_t TIMEMILLIS_1HOURll = TIMEMILLIS_1MINll * 60;
constexpr uint64_t TIMEMILLIS_1HOURull = TIMEMILLIS_1MINull * 60;

constexpr int64_t TIMEMILLIS_1DAYll = TIMEMILLIS_1HOURll * 24;
constexpr uint64_t TIMEMILLIS_1DAYull = TIMEMILLIS_1HOURull * 60;


// HANDLE 用 RAII

template<HANDLE InvalidHandleValue>
class HandleRAII
{
protected:
	HANDLE mHandle;

public:
	HandleRAII() : mHandle(InvalidHandleValue) { }
	HandleRAII(HANDLE argHandle) : mHandle(argHandle) { }

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

	virtual ~HandleRAII() { close(); }
};

class FileHandle : public HandleRAII<INVALID_HANDLE_VALUE>
{
public:
	using HandleRAII::HandleRAII;

	WINCSELIB_API BOOL setFileTime(const FSP_FSCTL_FILE_INFO& fileInfo);
	WINCSELIB_API BOOL setFileTime(UINT64 argCreationTime, UINT64 argLastWriteTime);
	WINCSELIB_API BOOL setBasicInfo(const FSP_FSCTL_FILE_INFO& fileInfo);
	WINCSELIB_API BOOL setBasicInfo(UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastWriteTime);
	WINCSELIB_API LONGLONG getFileSize();
};

class EventHandle : public HandleRAII<(HANDLE)NULL>
{
public:
	using HandleRAII<(HANDLE)NULL>::HandleRAII;
};

}

// インターフェース定義
#include "ICSService.hpp"
#include "ICSDriver.hpp"
#include "ICSDevice.hpp"
#include "ILogger.hpp"
#include "IWorker.hpp"

// 以降はアプリケーション関連

namespace WinCseLib {

//
// グローバル関数
//
WINCSELIB_API bool GetCacheFilePath(const std::wstring& argDir, const std::wstring& argName, std::wstring* pPath);
WINCSELIB_API bool PathToFileInfoW(const std::wstring& path, FSP_FSCTL_FILE_INFO* pFileInfo);
WINCSELIB_API bool PathToFileInfoA(const std::string& path, FSP_FSCTL_FILE_INFO* pFileInfo);
WINCSELIB_API bool MkdirIfNotExists(const std::wstring& dir);
WINCSELIB_API bool forEachFiles(const std::wstring& argDir, const std::function<void(const WIN32_FIND_DATA& wfd, const std::wstring& fullPath)>& callback);

WINCSELIB_API bool Base64EncodeA(const std::string& src, std::string* pDst);
WINCSELIB_API bool Base64DecodeA(const std::string& src, std::string* pDst);
WINCSELIB_API std::string URLEncodeA(const std::string& str);
WINCSELIB_API std::string URLDecodeA(const std::string& str);

WINCSELIB_API bool EncodeFileNameToLocalNameA(const std::string& src, std::string* pDst);
WINCSELIB_API bool DecodeLocalNameToFileNameA(const std::string& src, std::string* pDst);
WINCSELIB_API bool EncodeFileNameToLocalNameW(const std::wstring& src, std::wstring* pDst);
WINCSELIB_API bool DecodeLocalNameToFileNameW(const std::wstring& src, std::wstring* pDst);

//WINCSELIB_API bool HandleToPath(HANDLE Handle, std::wstring& wstr);
//WINCSELIB_API bool PathToSDStr(const std::wstring& path, std::wstring& sdstr);

WINCSELIB_API uint64_t UtcMillisToWinFileTime100ns(uint64_t utcMilliseconds);
WINCSELIB_API uint64_t WinFileTime100nsToUtcMillis(uint64_t fileTime100ns);

WINCSELIB_API uint64_t WinFileTimeToWinFileTime100ns(const FILETIME& ft);
WINCSELIB_API void WinFileTime100nsToWinFile(uint64_t ft100ns, FILETIME* ft);

WINCSELIB_API void UtcMillisToWinFileTime(uint64_t utcMilliseconds, FILETIME* ft);
WINCSELIB_API uint64_t WinFileTimeToUtcMillis(const FILETIME &ft);
WINCSELIB_API bool PathToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite);
WINCSELIB_API uint64_t GetCurrentUtcMillis();
WINCSELIB_API uint64_t GetCurrentWinFileTime100ns();

WINCSELIB_API long long int TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API long long int TimePointToUtcSecs(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp);

WINCSELIB_API std::wstring UtcMilliToLocalTimeStringW(uint64_t milliseconds);
WINCSELIB_API std::wstring WinFileTime100nsToLocalTimeStringW(uint64_t fileTime100ns);
WINCSELIB_API std::string WinFileTime100nsToLocalTimeStringA(uint64_t ft100ns);

WINCSELIB_API uint64_t STCTimeToUTCMilliSecW(const std::wstring& path);
WINCSELIB_API uint64_t STCTimeToWinFileTimeW(const std::wstring& path);
WINCSELIB_API uint64_t STCTimeToUTCMilliSecA(const std::string& path);
WINCSELIB_API uint64_t STCTimeToWinFileTimeA(const std::string& path);

WINCSELIB_API std::wstring MB2WC(const std::string& str);
WINCSELIB_API std::string WC2MB(const std::wstring& wstr);

WINCSELIB_API std::wstring TrimW(const std::wstring& str);
WINCSELIB_API std::string TrimA(const std::string& str);

WINCSELIB_API std::wstring WildcardToRegexW(const std::wstring& wildcard);
WINCSELIB_API std::string WildcardToRegexA(const std::string& wildcard);

WINCSELIB_API std::vector<std::wstring> SplitString(const std::wstring& input, const wchar_t sep, const bool ignoreEmpty);
WINCSELIB_API std::wstring JoinStrings(const std::vector<std::wstring>& tokens, const wchar_t sep, const bool ignoreEmpty);
WINCSELIB_API std::wstring ToUpper(const std::wstring& input);

WINCSELIB_API bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue);
WINCSELIB_API bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue);

WINCSELIB_API size_t HashString(const std::wstring& str);

WINCSELIB_API bool DecryptAES(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& encrypted, std::vector<BYTE>* pDecrypted);
WINCSELIB_API bool GetCryptKeyFromRegistryA(std::string* pOutput);
WINCSELIB_API bool GetCryptKeyFromRegistryW(std::wstring* pOutput);
WINCSELIB_API bool ComputeSHA256A(const std::string& input, std::string* pOutput);
WINCSELIB_API bool ComputeSHA256W(const std::wstring& input, std::wstring* pOutput);

WINCSELIB_API void AbnormalEnd(const char* file, const int line, const char* func, const int signum);
WINCSELIB_API int NamedWorkersToMap(NamedWorker workers[], std::unordered_map<std::wstring, IWorker*>* pWorkerMap);

//WINCSELIB_API NTSTATUS HandleToInfo(HANDLE handle, PUINT32 PFileAttributes /* nullable */, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize /* nullable */);

WINCSELIB_API NTSTATUS HandleToSecurityInfo(HANDLE Handle,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize /* nullable */);


WINCSELIB_API bool CreateLogger(const wchar_t* argTempDir, const wchar_t* argTrcDir, const wchar_t* argDllType);
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
	const wchar_t* mFile;
	const int mLine;
	const wchar_t* mFunc;

public:
	LogBlock(const wchar_t* argFile, const int argLine, const wchar_t* argFunc);
	~LogBlock();

	int depth();
	static int getCount();
};

//
// GetLastError() 値の保存
//
class LastErrorBackup
{
	DWORD mLastError;

public:
	LastErrorBackup() : mLastError(::GetLastError())
	{
	}

	~LastErrorBackup()
	{
		::SetLastError(mLastError);
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

} // namespace WinCseLib

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
	WinCseLib::ICSDriver* pDriver;
	WINFSP_STATS stats;
}
WINFSP_IF;

WINCSELIB_API int WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WINFSP_IF* appif);

// -----------------------------
//
// マクロ定義
//
#define NEW_LOG_BLOCK() \
	::SetLastError(ERROR_SUCCESS); \
	WinCseLib::LogBlock logBlock_(__FILEW__, __LINE__, __FUNCTIONW__)

#define LOG_BLOCK()		logBlock_
#define LOG_DEPTH()		LOG_BLOCK().depth()

#define traceA(format, ...) \
	WinCseLib::GetLogger()->traceA_impl(LOG_DEPTH(), __FILE__, __LINE__, __FUNCTION__, format, __VA_ARGS__)

#define traceW(format, ...) \
	WinCseLib::GetLogger()->traceW_impl(LOG_DEPTH(), __FILEW__, __LINE__, __FUNCTIONW__, format, __VA_ARGS__)

#define APP_ASSERT(expr) \
    if (!(expr)) { \
        WinCseLib::AbnormalEnd(__FILE__, __LINE__, __FUNCTION__, -1); \
    }

#define FA_IS_DIR(fa)			((fa) & FILE_ATTRIBUTE_DIRECTORY)

#define BOOL_CSTRW(b)			((b) ? L"true" : L"false")
#define BOOL_CSTRA(b)			((b) ? "true" : "false")

// EOF
