#pragma once

#include "CSDeviceCommon.h"

namespace CSEDAS3
{

struct RuntimeEnv final
{
	explicit RuntimeEnv(
		int									argBucketCacheExpiryMin,
		const std::list<std::wregex>&		argBucketFilters,
		const std::wstring&					argClientGuid,
		CSELIB::FILETIME_100NS_T			argDefaultCommonPrefixTime,
		UINT32								argDefaultFileAttributes,
		const std::optional<std::wregex>&	argIgnoreFileNamePatterns,
		int									argMaxDisplayBuckets,
		int									argMaxDisplayObjects,
		int									argObjectCacheExpiryMin,
		const								std::wstring argClientRegion,
		bool								argStrictBucketRegion,
		bool								argStrictFileTimestamp) noexcept
		:
		BucketCacheExpiryMin				(argBucketCacheExpiryMin),
		BucketFilters						(argBucketFilters),
		ClientGuid							(argClientGuid),
		DefaultCommonPrefixTime				(argDefaultCommonPrefixTime),
		DefaultFileAttributes				(argDefaultFileAttributes),
		IgnoreFileNamePatterns				(argIgnoreFileNamePatterns),
		MaxDisplayBuckets					(argMaxDisplayBuckets),
		MaxDisplayObjects					(argMaxDisplayObjects),
		ObjectCacheExpiryMin				(argObjectCacheExpiryMin),
		ClientRegion						(argClientRegion),
		StrictBucketRegion					(argStrictBucketRegion),
		StrictFileTimestamp					(argStrictFileTimestamp)
	{
	}

	const int								BucketCacheExpiryMin;
	const std::list<std::wregex>			BucketFilters;
	const std::wstring						ClientGuid;
	const CSELIB::FILETIME_100NS_T			DefaultCommonPrefixTime;
	const UINT32							DefaultFileAttributes;
	const std::optional<std::wregex>		IgnoreFileNamePatterns;
	const int								MaxDisplayBuckets;
	const int								MaxDisplayObjects;
	const int								ObjectCacheExpiryMin;
	const std::wstring						ClientRegion;
	const bool								StrictBucketRegion;
	const bool								StrictFileTimestamp;

	std::wstring str() const noexcept;
};

}	// namespace CSEDAS3

// EOF