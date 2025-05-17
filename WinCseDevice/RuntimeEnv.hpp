#pragma once

#include "CSDeviceCommon.h"

namespace CSEDVC
{

struct RuntimeEnv final
{
	WINCSEDEVICE_API RuntimeEnv(
		int									argBucketCacheExpiryMin,
		const std::list<std::wregex>&		argBucketFilters,
		const std::wstring&					argClientGuid,
		CSELIB::FILETIME_100NS_T			argDefaultCommonPrefixTime,
		const std::optional<std::wregex>&	argIgnoreFileNamePatterns,
		int									argMaxApiRetryCount,
		int									argMaxDisplayBuckets,
		int									argMaxDisplayObjects,
		int									argObjectCacheExpiryMin,
		bool								argStrictBucketRegion,
		bool								argStrictFileTimestamp,
		int									argTransferWriteSizeMib)
		:
		BucketCacheExpiryMin				(argBucketCacheExpiryMin),
		BucketFilters						(argBucketFilters),
		ClientGuid							(argClientGuid),
		DefaultCommonPrefixTime				(argDefaultCommonPrefixTime),
		IgnoreFileNamePatterns				(argIgnoreFileNamePatterns),
		MaxApiRetryCount					(argMaxApiRetryCount),
		MaxDisplayBuckets					(argMaxDisplayBuckets),
		MaxDisplayObjects					(argMaxDisplayObjects),
		ObjectCacheExpiryMin				(argObjectCacheExpiryMin),
		StrictBucketRegion					(argStrictBucketRegion),
		StrictFileTimestamp					(argStrictFileTimestamp),
		TransferWriteSizeMib				(argTransferWriteSizeMib)
	{
	}

	const int								BucketCacheExpiryMin;
	const std::list<std::wregex>			BucketFilters;
	const std::wstring						ClientGuid;
	const CSELIB::FILETIME_100NS_T			DefaultCommonPrefixTime;
	const std::optional<std::wregex>		IgnoreFileNamePatterns;
	const int								MaxApiRetryCount;
	const int								MaxDisplayBuckets;
	const int								MaxDisplayObjects;
	const int								ObjectCacheExpiryMin;
	const bool								StrictBucketRegion;
	const bool								StrictFileTimestamp;
	const int								TransferWriteSizeMib;

	WINCSEDEVICE_API std::wstring str() const;
	WINCSEDEVICE_API bool matchesBucketFilter(const std::wstring& argBucketName) const;
	WINCSEDEVICE_API bool shouldIgnoreWinPath(const std::filesystem::path& argWinPath) const;
};

}	// namespace CSESS3

// EOF