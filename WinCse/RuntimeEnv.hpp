#pragma once

#include "CSDriverCommon.h"

namespace CSEDRV
{

struct RuntimeEnv final
{
	explicit RuntimeEnv(
		const std::filesystem::path&		argCacheDataDir,
		int									argCacheFileRetentionMin,
		const std::filesystem::path&		argCacheReportDir,
		CSELIB::FILETIME_100NS_T			argDefaultCommonPrefixTime,
		UINT32								argDefaultFileAttributes,
		bool								argDeleteAfterUpload,
		int									argDeleteDirCondition,
		CSELIB::FileHandle&&				argDirSecurityRef,
		CSELIB::FileHandle&&				argFileSecurityRef,
		bool								argReadOnly,
		int									argTransferReadSizeMib)
		:
		CacheDataDir						(argCacheDataDir),
		CacheFileRetentionMin				(argCacheFileRetentionMin),
		CacheReportDir						(argCacheReportDir),
		DefaultCommonPrefixTime				(argDefaultCommonPrefixTime),
		DefaultFileAttributes				(argDefaultFileAttributes),
		DeleteAfterUpload					(argDeleteAfterUpload),
		DeleteDirCondition					(argDeleteDirCondition),
		DirSecurityRef						(std::move(argDirSecurityRef)),
		FileSecurityRef						(std::move(argFileSecurityRef)),
		ReadOnly							(argReadOnly),
		TransferReadSizeMib					(argTransferReadSizeMib)
	{
	}

	const std::filesystem::path				CacheDataDir;
	const int								CacheFileRetentionMin;
	const std::filesystem::path				CacheReportDir;
	const CSELIB::FILETIME_100NS_T			DefaultCommonPrefixTime;
	const UINT32							DefaultFileAttributes;
	const bool								DeleteAfterUpload;
	const int								DeleteDirCondition;
	const CSELIB::FileHandle				DirSecurityRef;
	const CSELIB::FileHandle				FileSecurityRef;
	const bool								ReadOnly;
	const int								TransferReadSizeMib;

	std::wstring str() const;
};

}	// namespace CSELIB

	// EOF