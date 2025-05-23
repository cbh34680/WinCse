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
#define WINCSELIB_API			__declspec(dllexport)
#else
#define WINCSELIB_API			__declspec(dllimport)
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

#include <exception>
#include <list>
#include <string>

//
// passthrough.c に定義されていたもののうち、アプリケーションに
// 必要となるものを外だし
//
#define ALLOCATION_UNIT         (4096)

#include "ICSService.hpp"

namespace CSELIB {

struct IFileContext
{
	virtual ~IFileContext() = default;
};

}

#include "ICSDriver.hpp"

typedef struct
{
	long SvcStart;
	long SvcStop;

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
	CSELIB::ICSDriver*	mDriver;
	WINFSP_STATS		FspStats;
}
WINCSE_IF;

extern "C"
{
	WINCSELIB_API NTSTATUS GetFileInfoInternal(HANDLE Handle, FSP_FSCTL_FILE_INFO* pFileInfo);
	WINCSELIB_API int WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WINCSE_IF* appif);
}

namespace CSELIB
{

struct FatalError : public std::exception
{
	const std::string	mWhat;
	const NTSTATUS		mNtstatus;

	explicit FatalError(const std::string& argWhat, DWORD argLastError)
		:
		mWhat(argWhat), mNtstatus(FspNtStatusFromWin32(argLastError))
	{
	}

	explicit FatalError(const std::string& argWhat, NTSTATUS argNtstatus)
		:
		mWhat(argWhat),
		mNtstatus(argNtstatus)
	{
	}

	explicit FatalError(const std::string& argWhat)
		: mWhat(argWhat), mNtstatus(STATUS_UNSUCCESSFUL)
	{
	}

	const char* what() const override
	{
		return mWhat.c_str();
	}

	WINCSELIB_API std::wstring whatW() const;
};

}

// EOF