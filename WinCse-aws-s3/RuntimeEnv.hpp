#pragma once

#include "WinCseLib.h"

struct RuntimeEnv
{
	explicit RuntimeEnv(
		int								argBucketCacheExpiryMin,
		const std::vector<std::wregex>&	argBucketFilters,
		const std::wstring&				argCacheDataDir,
		const std::wstring&				argCacheReportDir,
		int								argCacheFileRetentionMin,
		const std::wstring&				argClientGuid,
		UINT64							argDefaultCommonPrefixTime,
		UINT32							argDefaultFileAttributes,
		bool							argDeleteAfterUpload,
		int								argDeleteDirCondition,
		int								argMaxDisplayBuckets,
		int								argMaxDisplayObjects,
		int								argObjectCacheExpiryMin,
		bool							argReadOnly,
		const							std::wstring argClientRegion,
		bool							argStrictBucketRegion,
		bool							argStrictFileTimestamp) noexcept
		:
		BucketCacheExpiryMin			(argBucketCacheExpiryMin),
		BucketFilters					(argBucketFilters),
		CacheDataDir					(argCacheDataDir),
		CacheReportDir					(argCacheReportDir),
		CacheFileRetentionMin			(argCacheFileRetentionMin),
		ClientGuid						(argClientGuid),
		DefaultCommonPrefixTime			(argDefaultCommonPrefixTime),
		DefaultFileAttributes			(argDefaultFileAttributes),
		DeleteAfterUpload				(argDeleteAfterUpload),
		DeleteDirCondition				(argDeleteDirCondition),
		MaxDisplayBuckets				(argMaxDisplayBuckets),
		MaxDisplayObjects				(argMaxDisplayObjects),
		ObjectCacheExpiryMin			(argObjectCacheExpiryMin),
		ReadOnly						(argReadOnly),
		ClientRegion					(argClientRegion),
		StrictBucketRegion				(argStrictBucketRegion),
		StrictFileTimestamp				(argStrictFileTimestamp)
	{
	}

	const int							BucketCacheExpiryMin;
	const std::vector<std::wregex>		BucketFilters;
	const std::wstring					CacheDataDir;
	const std::wstring					CacheReportDir;
	const int							CacheFileRetentionMin;
	const std::wstring					ClientGuid;
	const UINT64						DefaultCommonPrefixTime;
	const UINT32						DefaultFileAttributes;
	const bool							DeleteAfterUpload;
	const int							DeleteDirCondition;
	const int							MaxDisplayBuckets;
	const int							MaxDisplayObjects;
	const int							ObjectCacheExpiryMin;
	const bool							ReadOnly;
	const std::wstring					ClientRegion;
	const bool							StrictBucketRegion;
	const bool							StrictFileTimestamp;

	std::wstring str() const noexcept;
};

// EOF