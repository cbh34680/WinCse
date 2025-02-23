#pragma once

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

#ifndef _RELEASE
#ifndef _DEBUG
#define _RELEASE	(1)
#endif
#endif

#define CALLER_ARG0			const wchar_t* caller_
#define CALLER_ARG			CALLER_ARG0,

// インターフェース定義
#include "IService.hpp"
#include "IStorageService.hpp"
#include "ICloudStorage.hpp"
#include "ILogger.hpp"
#include "IWorker.hpp"

// 以降はアプリケーション関連
#include <string>
#include <atomic>
#include <chrono>
#include <typeinfo>

namespace WinCseLib {

//
// グローバル関数
//
WINCSELIB_API bool TouchIfNotExists(const std::wstring& arg);
WINCSELIB_API bool MkdirIfNotExists(const std::wstring& dir);

WINCSELIB_API std::string Base64EncodeA(const std::string& data);
WINCSELIB_API std::string Base64DecodeA(const std::string& encodedData);
WINCSELIB_API std::string URLEncodeA(const std::string& str);
WINCSELIB_API std::string URLDecodeA(const std::string& str);

WINCSELIB_API std::string EncodeFileNameToLocalNameA(const std::string& str);
WINCSELIB_API std::string DecodeLocalNameToFileNameA(const std::string& str);
WINCSELIB_API std::wstring EncodeFileNameToLocalNameW(const std::wstring& str);
WINCSELIB_API std::wstring DecodeLocalNameToFileNameW(const std::wstring& str);

WINCSELIB_API bool HandleToPath(HANDLE Handle, std::wstring& wstr);
WINCSELIB_API bool PathToSDStr(const std::wstring& path, std::wstring& sdstr);;

WINCSELIB_API uint64_t UtcMillisToWinFileTimeIn100ns(uint64_t utcMilliseconds);
WINCSELIB_API uint64_t WinFileTimeIn100ns(const FILETIME& ft);
WINCSELIB_API void UtcMillisToWinFileTime(uint64_t utcMilliseconds, FILETIME* ft);
WINCSELIB_API uint64_t WinFileTimeToUtcMillis(const FILETIME &ft);
WINCSELIB_API bool HandleToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite);
WINCSELIB_API uint64_t GetCurrentUtcMillis();

WINCSELIB_API long long int TimePointToUtcSecs(const std::chrono::system_clock::time_point& tp);

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

WINCSELIB_API std::vector<std::wstring> SplitW(const std::wstring& input, const wchar_t sep, const bool ignoreEmpty);

WINCSELIB_API bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue);
WINCSELIB_API bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue);

WINCSELIB_API size_t HashString(const std::wstring& str);

WINCSELIB_API void AbnormalEnd(const char* file, const int line, const char* func, const int signum);

WINCSELIB_API bool CreateLogger(const wchar_t* argTempDir, const wchar_t* argTrcDir, const wchar_t* argDllType);
WINCSELIB_API ILogger* GetLogger();
WINCSELIB_API void DeleteLogger();

//
// ログ・ブロックの情報
//
class WINCSELIB_API LogBlock
{
	const wchar_t* file;
	const int line;
	const wchar_t* func;

public:
	LogBlock(const wchar_t* argFile, const int argLine, const wchar_t* argFunc);
	~LogBlock();

	int depth();

	static int getCount();
};

} // namespace WinCseLib

int WINCSELIB_API WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WinCseLib::IStorageService* cs);

// -----------------------------
//
// マクロ定義
//
#define APP_ASSERT(expr) \
    if (!(expr)) { \
        WinCseLib::AbnormalEnd(__FILE__, __LINE__, __FUNCTION__, -1); \
    }

#define INIT_CALLER0    __FUNCTIONW__
#define INIT_CALLER		INIT_CALLER0,

#define CALL_CHAIN()	(std::wstring(caller_) + L"->" + __FUNCTIONW__).c_str()
#define CONT_CALLER0	CALL_CHAIN()
#define CONT_CALLER		CONT_CALLER0,

#define NEW_LOG_BLOCK() \
	::SetLastError(ERROR_SUCCESS); \
	WinCseLib::LogBlock logBlock_(__FILEW__, __LINE__, __FUNCTIONW__)

#define LOG_BLOCK()		logBlock_
#define LOG_DEPTH()		LOG_BLOCK().depth()

#define traceA(format, ...) \
	WinCseLib::GetLogger()->traceA_impl(LOG_DEPTH(), __FILE__, __LINE__, __FUNCTION__, format, __VA_ARGS__)

#define traceW(format, ...) \
	WinCseLib::GetLogger()->traceW_impl(LOG_DEPTH(), __FILEW__, __LINE__, __FUNCTIONW__, format, __VA_ARGS__)


// EOF
