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
		bool								argDeleteAfterUpload,
		int									argDeleteDirCondition,
		CSELIB::FileHandle&&				argDirSecurityRef,
		CSELIB::FileHandle&&				argFileSecurityRef,
		bool								argReadOnly,
		int									argTransferPerSizeMib) noexcept
		:
		CacheDataDir						(argCacheDataDir),
		CacheFileRetentionMin				(argCacheFileRetentionMin),
		CacheReportDir						(argCacheReportDir),
		DeleteAfterUpload					(argDeleteAfterUpload),
		DeleteDirCondition					(argDeleteDirCondition),
		DirSecurityRef						(std::move(argDirSecurityRef)),
		FileSecurityRef						(std::move(argFileSecurityRef)),
		ReadOnly							(argReadOnly),
		TransferPerSizeMib					(argTransferPerSizeMib)
	{
	}

	const std::filesystem::path				CacheDataDir;
	const int								CacheFileRetentionMin;
	const std::filesystem::path				CacheReportDir;
	const bool								DeleteAfterUpload;
	const int								DeleteDirCondition;
	const CSELIB::FileHandle				DirSecurityRef;
	const CSELIB::FileHandle				FileSecurityRef;
	const bool								ReadOnly;
	const int								TransferPerSizeMib;

	std::wstring str() const noexcept;
};

}	// namespace CSELIB

	// EOF