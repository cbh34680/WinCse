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
#include <unordered_map>
#include <mutex>

typedef std::shared_ptr<FSP_FSCTL_DIR_INFO> DirInfoType;
typedef std::list<DirInfoType> DirInfoListType;

#define CALLER_ARG0			const std::wstring& caller_
#define CALLER_ARG			CALLER_ARG0,

namespace WinCseLib {

#if 0
class FileHandleRAII
{
	HANDLE mHandle;

public:
	HANDLE handle() const { return mHandle; }

	bool invalid() const { return mHandle == INVALID_HANDLE_VALUE; }
	bool valid() const { return !invalid(); }

	FileHandleRAII() : mHandle(INVALID_HANDLE_VALUE) { }
	FileHandleRAII(HANDLE argHandle) : mHandle(argHandle) { }

	FileHandleRAII& operator=(HANDLE argHandle)
	{
		close();
		mHandle = argHandle;

		return *this;
	}

	void close()
	{
		if (mHandle != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(mHandle);
			mHandle = INVALID_HANDLE_VALUE;
		}
	}

	~FileHandleRAII() { close(); }
};

class EventHandleRAII
{
	HANDLE mHandle;

public:
	HANDLE handle() const { return mHandle; }

	bool invalid() const { return !mHandle; }
	bool valid() const { return mHandle; }

	EventHandleRAII() : mHandle(NULL) { }
	EventHandleRAII(HANDLE argHandle) : mHandle(argHandle) { }

	EventHandleRAII& operator=(HANDLE argHandle)
	{
		close();
		mHandle = argHandle;

		return *this;
	}

	void close()
	{
		if (mHandle)
		{
			::CloseHandle(mHandle);
			mHandle = NULL;
		}
	}

	~EventHandleRAII() { close(); }
};

#else
template<HANDLE InvalidHandleValue>
class HandleRAII
{
	HANDLE mHandle;

protected:
	HandleRAII() : mHandle(InvalidHandleValue) { }
	HandleRAII(HANDLE argHandle) : mHandle(argHandle) { }

	void assign(HANDLE argHandle)
	{
		close();
		mHandle = argHandle;
	}

public:
	HANDLE handle() const { return mHandle; }
	bool invalid() const { return mHandle == InvalidHandleValue; }
	bool valid() const { return !invalid(); }

	void close()
	{
		if (mHandle != InvalidHandleValue)
		{
			::CloseHandle(mHandle);
			mHandle = InvalidHandleValue;
		}
	}

	virtual ~HandleRAII() { close(); }
};

//
// �R���X�g���N�^��f�X�g���N�^���ŉ��z�֐����Ăяo���ƁA���z�֐��͔h���N���X�ł͂Ȃ�
// ���N���X�̃o�[�W�����ŉ�������Ă��܂��̂ŁA�����l���R���X�g���N�^�œn��
// 
// --> �l�ɂ��e���v���[�g�ɕύX
//

//
// ���N���X (HandleRAII) �� operator=() ���������Ă��A�h���N���X (FileHandleRAII) ��
// �R�s�[�R���X�g���N�^������ƁA�ꎞ�I�u�W�F�N�g�̐����ƃR�s�[�R���X�g���N�^�̌Ăяo�����s����
// ���̂��߁A�h���N���X�� operator=() ��p�ӂ���
//

class FileHandleRAII : public HandleRAII<INVALID_HANDLE_VALUE>
{
public:
	FileHandleRAII() {}
	FileHandleRAII(HANDLE argHandle) : HandleRAII(argHandle) { }

	FileHandleRAII& operator=(HANDLE argHandle)
	{
		assign(argHandle);
		return *this;
	}
};

class EventHandleRAII : public HandleRAII<(HANDLE)NULL>
{
public:
	EventHandleRAII() {}
	EventHandleRAII(HANDLE argHandle) : HandleRAII(argHandle) { }

	EventHandleRAII& operator=(HANDLE argHandle)
	{
		assign(argHandle);
		return *this;
	}
};

#endif

}

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

WINCSELIB_API std::vector<std::wstring> SplitString(const std::wstring& input, const wchar_t sep, const bool ignoreEmpty);
WINCSELIB_API std::wstring JoinStrings(const std::vector<std::wstring>& tokens, const wchar_t sep, const bool ignoreEmpty);
WINCSELIB_API std::wstring ToUpper(const std::wstring& input);

WINCSELIB_API bool GetIniStringW(const std::wstring& confPath, const wchar_t* argSection, const wchar_t* keyName, std::wstring* pValue);
WINCSELIB_API bool GetIniStringA(const std::string& confPath, const char* argSection, const char* keyName, std::string* pValue);

WINCSELIB_API size_t HashString(const std::wstring& str);

WINCSELIB_API bool DecryptAES(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& encrypted, std::vector<BYTE>* pDecrypted);
WINCSELIB_API bool GetCryptKeyFromRegistry(std::string* pKeyStr);

WINCSELIB_API void AbnormalEnd(const char* file, const int line, const char* func, const int signum);

WINCSELIB_API bool CreateLogger(const wchar_t* argTempDir, const wchar_t* argTrcDir, const wchar_t* argDllType);
WINCSELIB_API ILogger* GetLogger();
WINCSELIB_API void DeleteLogger();

// �t�@�C�������� FSP_FSCTL_DIR_INFO �̃q�[�v�̈�𐶐����A�������̃����o��ݒ肵�ĕԋp
WINCSELIB_API DirInfoType makeDirInfo(const ObjectKey& argObjKey);

WINCSELIB_API bool SplitPath(const std::wstring& argKey,
    std::wstring* pParentDir /* nullable */, std::wstring* pFilename /* nullable */);

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

template<typename T> class UnprotectedShare;
template<typename T> class ProtectedShare;

class SharedBase
{
	std::mutex mMutex;
	int mRefCount = 0;

	template<typename T> friend class UnprotectedShare;
	template<typename T> friend class ProtectedShare;
};

template<typename T>
struct ShareStore
{
	std::mutex mMapGuard;
	std::unordered_map<std::wstring, std::unique_ptr<T>> mMap;
};

template<typename T>
class ProtectedShare
{
	T* mV;

	template<typename T> friend class UnprotectedShare;

	ProtectedShare(T* argV) : mV(argV)
	{
		mV->mMutex.lock();
	}

public:
	~ProtectedShare()
	{
		unlock();
	}

	void unlock()
	{
		if (mV)
		{
#pragma warning(suppress: 26110)
			mV->mMutex.unlock();
			mV = nullptr;
		}
	}

	T* operator->() {
		return mV;
	}

	const T* operator->() const {
		return mV;
	}
};

template<typename T>
class UnprotectedShare
{
	ShareStore<T>* mStore;
	const std::wstring mName;
	T* mV = nullptr;

public:
	template<typename... Args>
	UnprotectedShare(ShareStore<T>* argStore, const std::wstring& argName, Args... args)
		: mStore(argStore), mName(argName)
	{
		std::lock_guard<std::mutex> _(mStore->mMapGuard);

		auto it{ mStore->mMap.find(mName) };
		if (it == mStore->mMap.end())
		{
			it = mStore->mMap.emplace(mName, std::make_unique<T>(args...)).first;
		}

		it->second->mRefCount++;

		static_assert(std::is_base_of<SharedBase, T>::value, "T must be derived from SharedBase");

		mV = dynamic_cast<T*>(it->second.get());
		_ASSERT(mV);
	}

	~UnprotectedShare()
	{
		std::lock_guard<std::mutex> _(mStore->mMapGuard);

		auto it{ mStore->mMap.find(mName) };

		it->second->mRefCount--;

		if (it->second->mRefCount == 0)
		{
			mStore->mMap.erase(it);
		}
	}

	ProtectedShare<T> lock()
	{
		return ProtectedShare<T>(this->mV);
	}
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

WINCSELIB_API int WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WINFSP_IF* appif);

// -----------------------------
//
// �}�N����`
//
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

#define APP_ASSERT(expr) \
    if (!(expr)) { \
        WinCseLib::AbnormalEnd(__FILE__, __LINE__, __FUNCTION__, -1); \
    }

#define FA_IS_DIR(fa)		((fa) & FILE_ATTRIBUTE_DIRECTORY)

#define BOOL_CSTRW(b)	((b) ? L"true" : L"false")

// EOF
