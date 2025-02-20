#pragma once

#ifndef _RELEASE
#ifndef _DEBUG
#define _RELEASE	(1)
#endif
#endif

#define CALLER_ARG0			const wchar_t* caller_
#define CALLER_ARG			CALLER_ARG0,

//
// "storage_service_if.h" で malloc などの再定義をしているので、早い段階で include する
//
#include "storage_service_if.hpp"

// インターフェース定義
#include "cloud_storage_if.hpp"
#include "logger_if.hpp"
#include "worker_if.hpp"

// 以降はアプリケーション関連
#include <string>
#include <atomic>
#include <chrono>
#include <typeinfo>


#define APP_ASSERT(expr) \
    if (!(expr)) { \
        app_assert(__FILE__, __LINE__, __FUNCTION__, -1); \
        abort(); \
    }

void app_assert(const char* file, const int line, const char* func, const int signo);

//

// ログ・ブロックの情報
class LogBlock
{
	static std::atomic<int> mCounter;
	static thread_local int mDepth;

	std::chrono::steady_clock::time_point start;

	const wchar_t* file;
	const int line;
	const wchar_t* func;

public:
	LogBlock(const wchar_t* argFile, const int argLine, const wchar_t* argFunc);
	~LogBlock();

	int depth() { return mDepth; }

	static int getCount()
	{
		return mCounter.load();
	}
};

// 文字列をバケット名とキーに分割
struct BucketKey
{
	std::wstring bucket;
	std::wstring key;

	bool HasKey = false;
	bool OK = false;

	BucketKey(const wchar_t* wstr);
};

// -----------------------------
//
// グローバル関数
//

#define INIT_CALLER		__FUNCTIONW__,

#define CALL_CHAIN()	(std::wstring(caller_) + L"->" + __FUNCTIONW__).c_str()
#define CONT_CALLER0	CALL_CHAIN()
#define CONT_CALLER		CONT_CALLER0,

#define NEW_LOG_BLOCK() \
	::SetLastError(ERROR_SUCCESS); \
	LogBlock logBlock_(__FILEW__, __LINE__, __FUNCTIONW__)

#define LOG_BLOCK()		logBlock_
#define LOG_DEPTH()		LOG_BLOCK().depth()

#define traceA(format, ...) \
	GetLogger()->traceA_impl(LOG_DEPTH(), __FILE__, __LINE__, __FUNCTION__, format, __VA_ARGS__)

#define traceW(format, ...) \
	GetLogger()->traceW_impl(LOG_DEPTH(), __FILEW__, __LINE__, __FUNCTIONW__, format, __VA_ARGS__)


template <typename T>
std::string getDerivedClassNames(T* baseClass)
{
	const std::type_info& typeInfo = typeid(*baseClass);
	return typeInfo.name();
}

ILogger* GetLogger();

bool touchIfNotExists(const std::wstring& arg);
bool mkdirIfNotExists(const std::wstring& dir);

std::string Base64EncodeA(const std::string& data);
std::string URLEncodeA(const std::string& str);

std::string EncodeFileNameToLocalNameA(const std::string& str);
std::string DecodeLocalNameToFileNameA(const std::string& str);
std::wstring EncodeFileNameToLocalNameW(const std::wstring& str);
std::wstring DecodeLocalNameToFileNameW(const std::wstring& str);

bool HandleToPath(HANDLE Handle, std::wstring& wstr);
bool PathToSDStr(const std::wstring& path, std::wstring& sdstr);;

uint64_t UtcMillisToWinFileTime(uint64_t utcMilliseconds);
uint64_t WinFileTimeToUtcMillis(const FILETIME &ft);
uint64_t GetCurrentUtcMillis();

long long int TimePointToUtcSeconds(const std::chrono::system_clock::time_point& tp);

uint64_t STCTimeToUTCMilliSecW(const std::wstring& path);
uint64_t STCTimeToWinFileTimeW(const std::wstring& path);
uint64_t STCTimeToUTCMilliSecA(const std::string& path);
uint64_t STCTimeToWinFileTimeA(const std::string& path);

std::wstring MB2WC(const std::string& str);
std::string WC2MB(const std::wstring& wstr);

std::wstring TrimW(const std::wstring& str);
std::string TrimA(const std::string& str);

std::wstring WildcardToRegexW(const std::wstring& wildcard);
std::string WildcardToRegexA(const std::string& wildcard);

bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue);
bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue);

size_t HashString(const std::wstring& str);

// malloc, calloc で確保したメモリを shared_ptr で解放するための関数
template <typename T>
void free_deleter(T* ptr)
{
	free(ptr);
}

// EOF
