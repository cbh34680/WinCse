#pragma once

#ifndef _RELEASE
#ifndef _DEBUG
#define _RELEASE	(1)
#endif
#endif

#ifdef WINCSELIB_EXPORTS
#define WINCSELIB_API __declspec(dllexport)
#else
#define WINCSELIB_API __declspec(dllimport)
#endif

//
// ���������[�N���o�Ƀ\�[�X�R�[�h�����o�͂��邽�߂ɂ́A���̃t�@�C����
// ��ԍŏ��� include ���Ȃ���΂Ȃ�Ȃ�
//
#include "internal_define_alloc.h"
#include "WinFsp_c.h"

#include <string>
#include <list>
#include <vector>
#include <memory>
#include <chrono>

typedef std::shared_ptr<FSP_FSCTL_DIR_INFO> DirInfoType;
typedef std::list<DirInfoType> DirInfoListType;

#define CALLER_ARG0			const std::wstring& caller_
#define CALLER_ARG			CALLER_ARG0,

// �C���^�[�t�F�[�X��`
#include "ICSService.hpp"
#include "ICSDriver.hpp"
#include "ICSDevice.hpp"
#include "ILogger.hpp"
#include "IWorker.hpp"

// �ȍ~�̓A�v���P�[�V�����֘A

namespace WinCseLib {

//
// �O���[�o���֐�
//
WINCSELIB_API bool PathToFileInfo(const std::wstring& path, FSP_FSCTL_FILE_INFO* pFileInfo);
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

WINCSELIB_API uint64_t UtcMillisToWinFileTime100ns(uint64_t utcMilliseconds);
WINCSELIB_API uint64_t WinFileTime100nsToUtcMillis(uint64_t fileTime100ns);

WINCSELIB_API uint64_t WinFileTimeToWinFileTime100ns(const FILETIME& ft);
WINCSELIB_API void WinFileTime100nsToWinFile(uint64_t ft100ns, FILETIME* ft);

WINCSELIB_API void UtcMillisToWinFileTime(uint64_t utcMilliseconds, FILETIME* ft);
WINCSELIB_API uint64_t WinFileTimeToUtcMillis(const FILETIME &ft);
WINCSELIB_API bool PathToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite);
WINCSELIB_API uint64_t GetCurrentUtcMillis();

WINCSELIB_API long long int TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API long long int TimePointToUtcSecs(const std::chrono::system_clock::time_point& tp);
WINCSELIB_API std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp);

WINCSELIB_API std::wstring UtcMilliToLocalTimeStringW(uint64_t milliseconds);
WINCSELIB_API std::wstring WinFileTime100nsToLocalTimeStringW(uint64_t fileTime100ns);

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
WINCSELIB_API std::wstring JoinW(const std::vector<std::wstring>& tokens, const wchar_t sep, const bool ignoreEmpty);

WINCSELIB_API bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue);
WINCSELIB_API bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue);

WINCSELIB_API size_t HashString(const std::wstring& str);

WINCSELIB_API bool DecryptAES(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& encrypted, std::vector<BYTE>* pDecrypted);
WINCSELIB_API bool GetCryptKeyFromRegistry(std::string* pKeyStr);

WINCSELIB_API void AbnormalEnd(const char* file, const int line, const char* func, const int signum);

WINCSELIB_API bool CreateLogger(const wchar_t* argTempDir, const wchar_t* argTrcDir, const wchar_t* argDllType);
WINCSELIB_API ILogger* GetLogger();
WINCSELIB_API void DeleteLogger();

//
// ���O�E�u���b�N�̏��
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

int WINCSELIB_API WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WINFSP_IF* appif);

// -----------------------------
//
// �}�N����`
//
#define APP_ASSERT(expr) \
    if (!(expr)) { \
        WinCseLib::AbnormalEnd(__FILE__, __LINE__, __FUNCTION__, -1); \
    }

#define START_CALLER0   std::wstring(__FUNCTIONW__)
#define START_CALLER	START_CALLER0,

#define CALL_FROM()		(caller_)
#define CALL_CHAIN()	(CALL_FROM() + L"->" + __FUNCTIONW__)
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
