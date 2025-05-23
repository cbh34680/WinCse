#pragma once

#include "WinCseLib_c.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

#include "ILogger.hpp"

// ObjectKey.hpp に必要なものを定義 -->

namespace CSELIB {

WINCSELIB_API void AbnormalEnd(PCWSTR file, int line, PCWSTR func, int signum);

}	// namespace CSELIB

#define APP_ASSERT(expr) \
if (!(expr)) { \
    CSELIB::AbnormalEnd(__FILEW__, __LINE__, __FUNCTIONW__, -1); \
}

#define ALIGN_TO_UNIT(size, unit)	(((size) + (unit) - 1) / (unit) * (unit))
#define UNIT_COUNT(size, unit)		(((size) + (unit) - 1) / (unit))

#define FA_IS_DIR(fa)				((fa) & FILE_ATTRIBUTE_DIRECTORY)

#include "IWorker.hpp"

namespace CSELIB {

enum class FileTypeEnum
{
	None,
	Root,
	Bucket,
	Directory,
	File,
};

// ファイルの位置とサイズ

using FILEIO_OFFSET_T = INT64;
using FILEIO_LENGTH_T = INT64;
using FILESIZE_T = INT64;

// Windows FILETIME 100ns

using FILETIME_100NS_T = UINT64;

// UTC Milli Seconds

using UTC_MILLIS_T = UINT64;

}	// namespace CSELIB

// ObjectKey.hpp に必要なものを定義 <--

#include "ObjectKey.hpp"
#include "DirEntry.hpp"

#define CALLER_ARG0				[[maybe_unused]] const std::wstring& caller_
#define CALLER_ARG				CALLER_ARG0,

#define START_CALLER0			std::wstring(__FUNCTIONW__)
#define START_CALLER			START_CALLER0,

#define CALL_FROM()				(caller_)
#define CALL_CHAIN()			(CALL_FROM() + L"->" + __FUNCTIONW__)
#define CONT_CALLER0			CALL_CHAIN()
#define CONT_CALLER				CONT_CALLER0,

#include "ICSDevice.hpp"

// 以降はアプリケーション関連

namespace CSELIB {

//
// グローバル関数
//
WINCSELIB_API std::wstring MB2WC(const std::string& str);
WINCSELIB_API std::string WC2MB(const std::wstring& wstr);
WINCSELIB_API std::wstring SafeSubStringW(const std::wstring& str, std::wstring::size_type pos, std::wstring::size_type len=std::wstring::npos);
WINCSELIB_API std::string SafeSubStringA(const std::string& str, std::string::size_type pos, std::string::size_type len=std::string::npos);
WINCSELIB_API std::wstring TrimW(const std::wstring& str);
WINCSELIB_API std::vector<std::wstring> SplitString(const std::wstring& input, wchar_t sep, bool ignoreEmpty);
WINCSELIB_API bool SplitObjectKey(const std::wstring& argKey, std::wstring* pParentDir /* nullable */, std::wstring* pFileName /* nullable */);
WINCSELIB_API std::wstring FileTypeEnumToStringW(FileTypeEnum argFileType);
WINCSELIB_API std::wstring FileAttributesToStringW(DWORD dwFlagsAndAttributes);
WINCSELIB_API std::wstring FileInfoToStringW(const FSP_FSCTL_FILE_INFO& argFileInfo);

WINCSELIB_API FILESIZE_T GetFileSize(const std::filesystem::path& argPath);
WINCSELIB_API BOOL DeleteFilePassively(const std::filesystem::path& argPath);
WINCSELIB_API bool GetFileNameFromHandle(HANDLE hFile, std::filesystem::path* pPath);
WINCSELIB_API bool mkdirIfNotExists(const std::filesystem::path& argDir);
WINCSELIB_API bool forEachFiles(const std::filesystem::path& argDir, const std::function<void(const WIN32_FIND_DATA&, const std::filesystem::path&)>& callback);
WINCSELIB_API bool forEachDirs(const std::filesystem::path& argDir, const std::function<void(const WIN32_FIND_DATA&, const std::filesystem::path&)>& callback);

WINCSELIB_API FILETIME_100NS_T UtcMillisToWinFileTime100ns(UTC_MILLIS_T argUtcMillis);
WINCSELIB_API UTC_MILLIS_T WinFileTime100nsToUtcMillis(FILETIME_100NS_T ft100ns);
WINCSELIB_API FILETIME_100NS_T WinFileTimeToWinFileTime100ns(const FILETIME& ft);
WINCSELIB_API void WinFileTime100nsToWinFile(FILETIME_100NS_T ft100ns, FILETIME* pFileTime);
WINCSELIB_API void UtcMillisToWinFileTime(UTC_MILLIS_T argUtcMillis, FILETIME* pFileTime);
WINCSELIB_API UTC_MILLIS_T WinFileTimeToUtcMillis(const FILETIME &ft);
WINCSELIB_API UTC_MILLIS_T GetCurrentUtcMillis();
WINCSELIB_API FILETIME_100NS_T GetCurrentWinFileTime100ns();
WINCSELIB_API UTC_MILLIS_T TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API FILETIME_100NS_T TimePointToWinFileTime100ns(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API std::wstring UtcMillisToLocalTimeStringW(UTC_MILLIS_T argUtcMillis);
WINCSELIB_API std::wstring WinFileTime100nsToLocalTimeStringW(FILETIME_100NS_T ft100ns);
WINCSELIB_API std::string WinFileTime100nsToLocalTimeStringA(FILETIME_100NS_T ft100ns);
WINCSELIB_API std::wstring WinFileTimeToLocalTimeStringW(const FILETIME &ft);
WINCSELIB_API UTC_MILLIS_T STCTimeToUTCMillisW(const std::wstring& path);
WINCSELIB_API UTC_MILLIS_T STCTimeToUTCMilliSecA(const std::string& path);
WINCSELIB_API FILETIME_100NS_T STCTimeToWinFileTime100nsW(const std::wstring& path);

WINCSELIB_API std::wstring WildcardToRegexW(const std::wstring& wildcard);
WINCSELIB_API bool Base64DecodeA(const std::string& src, std::string* pDst);
WINCSELIB_API size_t HashString(const std::wstring& str);

WINCSELIB_API int GetIniIntW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, int defaultValue, int minValue, int maxValue);
WINCSELIB_API bool GetIniBoolW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, bool defaultValue);
WINCSELIB_API bool GetIniStringW(const std::filesystem::path& confPath, const std::wstring& argSection, PCWSTR keyName, std::wstring* pValue);

WINCSELIB_API std::wstring CreateGuidW();
WINCSELIB_API bool DecryptAES(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& encrypted, std::vector<BYTE>* pDecrypted);
WINCSELIB_API LSTATUS GetCryptKeyFromRegistryA(std::string* pOutput);
WINCSELIB_API LSTATUS GetCryptKeyFromRegistryW(std::wstring* pOutput);
WINCSELIB_API NTSTATUS ComputeSHA256A(const std::string& input, std::string* pOutput);
WINCSELIB_API NTSTATUS ComputeSHA256W(const std::wstring& input, std::wstring* pOutput);
WINCSELIB_API bool DecryptCredentialStringA(const std::string& argSecretKey, std::string* pInOut);
WINCSELIB_API bool DecryptCredentialStringW(const std::wstring& argSecretKey, std::wstring* pInOut);

WINCSELIB_API int NamedWorkersToMap(NamedWorker workers[], std::map<std::wstring, IWorker*>* pWorkerMap);

WINCSELIB_API NTSTATUS HandleToSecurityInfo(HANDLE Handle, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize /* nullable */);

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

template <typename BaseT>
std::string getDerivedClassNamesA(BaseT* baseClass)
{
	const std::type_info& typeInfo = typeid(*baseClass);
	return typeInfo.name();
}

template <typename BaseT>
std::wstring getDerivedClassNamesW(BaseT* baseClass)
{
	const std::type_info& typeInfo = typeid(*baseClass);
	return MB2WC(typeInfo.name());
}

template <typename MapType>
std::list<typename MapType::mapped_type> mapValues(const MapType& container)
{
	std::list<typename MapType::mapped_type> values;

	std::transform(container.cbegin(), container.cend(), std::back_inserter(values),
		[](const auto& pair) { return pair.second; });

	return values;
}

//
// ログ・ブロックの情報
//

class LogBlock final
{
	PCWSTR mFile;
	const int mLine;
	PCWSTR mFunc;

	static thread_local int mDepth;

public:
	WINCSELIB_API LogBlock(PCWSTR argFile, int argLine, PCWSTR argFunc);
	WINCSELIB_API ~LogBlock();

	WINCSELIB_API int depth() const;
};

//
// GetLastError() 値の保存
//
class KeepLastError final
{
	const DWORD mLastError;

public:
	KeepLastError()
		:
		mLastError(::GetLastError())
	{
	}

	~KeepLastError()
	{
		::SetLastError(mLastError);
	}
};

// ファイルサイズ

constexpr int32_t	FILESIZE_1B			= 1;
constexpr uint32_t	FILESIZE_1Bu		= 1U;

constexpr int32_t	FILESIZE_1KiB		= FILESIZE_1B		* 1024;
constexpr uint32_t	FILESIZE_1KiBu		= FILESIZE_1Bu		* 1024;

constexpr int32_t	FILESIZE_1MiB		= FILESIZE_1KiB		* 1024;
constexpr uint32_t	FILESIZE_1MiBu		= FILESIZE_1KiBu	* 1024;

constexpr int32_t	FILESIZE_1GiB		= FILESIZE_1MiB		* 1024;
constexpr uint32_t	FILESIZE_1GiBu		= FILESIZE_1MiBu	* 1024;

//
constexpr int64_t	FILESIZE_1Bll		= 1LL;
constexpr uint64_t	FILESIZE_1Bull		= 1ULL;

constexpr int64_t	FILESIZE_1KiBll		= FILESIZE_1Bll		* 1024;
constexpr uint64_t	FILESIZE_1KiBull	= FILESIZE_1Bull	* 1024;

constexpr int64_t	FILESIZE_1MiBll		= FILESIZE_1KiBll	* 1024;
constexpr uint64_t	FILESIZE_1MiBull	= FILESIZE_1KiBull	* 1024;

constexpr int64_t	FILESIZE_1GiBll		= FILESIZE_1MiBll	* 1024;
constexpr uint64_t	FILESIZE_1GiBull	= FILESIZE_1MiBull	* 1024;

// 時間 (DWORD)

constexpr int32_t	TIMEMILLIS_1SEC		= 1000;
constexpr uint32_t	TIMEMILLIS_1SECu	= 1000U;

constexpr int32_t	TIMEMILLIS_1MIN		= TIMEMILLIS_1SEC	* 60;
constexpr uint32_t	TIMEMILLIS_1MINu	= TIMEMILLIS_1SECu	* 60;

constexpr int32_t	TIMEMILLIS_1HOUR	= TIMEMILLIS_1MIN	* 60;
constexpr uint32_t	TIMEMILLIS_1HOURu	= TIMEMILLIS_1MINu	* 60;

constexpr int32_t	TIMEMILLIS_1DAY		= TIMEMILLIS_1HOUR	* 24;
constexpr uint32_t	TIMEMILLIS_1DAYu	= TIMEMILLIS_1HOURu	* 24;

// 時間 (INT64, UINT64)

constexpr int64_t	TIMEMILLIS_1SECll	= 1000LL;
constexpr uint64_t	TIMEMILLIS_1SECull	= 1000ULL;

constexpr int64_t	TIMEMILLIS_1MINll	= TIMEMILLIS_1SECll		* 60;
constexpr uint64_t	TIMEMILLIS_1MINull	= TIMEMILLIS_1SECull	* 60;

constexpr int64_t	TIMEMILLIS_1HOURll	= TIMEMILLIS_1MINll		* 60;
constexpr uint64_t	TIMEMILLIS_1HOURull	= TIMEMILLIS_1MINull	* 60;

constexpr int64_t	TIMEMILLIS_1DAYll	= TIMEMILLIS_1HOURll	* 24;
constexpr uint64_t	TIMEMILLIS_1DAYull	= TIMEMILLIS_1HOURull	* 24;

//
constexpr size_t FILEIO_BUFFER_SIZE = 1024ULL * 1024;

// 設定ファイル名

constexpr const wchar_t* const CONFIGFILE_FNAME = L"WinCse.conf";

} // namespace CSELIB

#include "Handle.hpp"
#include "FilePart.hpp"

// -----------------------------
//
// マクロ定義
//

#define LOG_BLOCK()					LogBlock_internal

#define NEW_LOG_BLOCK()				CSELIB::LogBlock LOG_BLOCK()(__FILEW__, __LINE__, __FUNCTIONW__)
#define LOG_DEPTH()					LOG_BLOCK().depth()

#ifdef _DEBUG
#define traceW(format, ...)			do { auto* logger_ = CSELIB::GetLogger(); const auto text_ = logger_->makeTextW(LOG_DEPTH(), __FILEW__, __LINE__, __FUNCTIONW__, ::GetLastError(), format, __VA_ARGS__); logger_->writeToTraceLog(text_); } while (false)
#define traceA(format, ...)			do { auto* logger_ = CSELIB::GetLogger(); const auto text_ = logger_->makeTextA(LOG_DEPTH(), __FILE__,  __LINE__, __FUNCTION__,  ::GetLastError(), format, __VA_ARGS__); logger_->writeToTraceLog(text_); } while (false)
#define errorW(format, ...)			do { auto* logger_ = CSELIB::GetLogger(); const auto text_ = logger_->makeTextW(LOG_DEPTH(), __FILEW__, __LINE__, __FUNCTIONW__, ::GetLastError(), format, __VA_ARGS__); logger_->writeToTraceLog(text_); logger_->writeToErrorLog(text_); } while (false)
#define errorA(format, ...)			do { auto* logger_ = CSELIB::GetLogger(); const auto text_ = logger_->makeTextA(LOG_DEPTH(), __FILE__,  __LINE__, __FUNCTION__,  ::GetLastError(), format, __VA_ARGS__); logger_->writeToTraceLog(text_); logger_->writeToErrorLog(text_); } while (false)

#else
// リリース時はエラーログのみ

#define traceW(format, ...)
#define traceA(format, ...)
#define errorW(format, ...)			do { auto* logger_ = CSELIB::GetLogger(); const auto text_ = logger_->makeTextW(LOG_DEPTH(), __FILEW__, __LINE__, __FUNCTIONW__, ::GetLastError(), format, __VA_ARGS__); logger_->writeToErrorLog(text_); } while (false)
#define errorA(format, ...)			do { auto* logger_ = CSELIB::GetLogger(); const auto text_ = logger_->makeTextA(LOG_DEPTH(), __FILE__,  __LINE__, __FUNCTION__,  ::GetLastError(), format, __VA_ARGS__); logger_->writeToErrorLog(text_); } while (false)

#endif

//#define FA_MEANS_TEMPORARY(fa)		((fa) & FILE_ATTRIBUTE_HIDDEN || (fa) & FILE_ATTRIBUTE_TEMPORARY || (fa) & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)

#define BOOL_CSTRW(b)				((b) ? L"true" : L"false")
#define BOOL_CSTRA(b)				((b) ? "true" : "false")

// EOF
