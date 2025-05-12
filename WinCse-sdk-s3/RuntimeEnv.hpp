#pragma once

#include "SdkS3Common.h"

namespace CSESS3
{

struct RuntimeEnv final
{
	WINCSESDKS3_API RuntimeEnv(
		int									argBucketCacheExpiryMin,
		const std::list<std::wregex>&		argBucketFilters,
		const std::wstring&					argClientGuid,
		CSELIB::FILETIME_100NS_T			argDefaultCommonPrefixTime,
		const std::optional<std::wregex>&	argIgnoreFileNamePatterns,
		int									argMaxDisplayBuckets,
		int									argMaxDisplayObjects,
		int									argObjectCacheExpiryMin,
		const								std::wstring argClientRegion,
		bool								argStrictBucketRegion,
		bool								argStrictFileTimestamp,
		int									argTransferWriteSizeMib)
		:
		BucketCacheExpiryMin				(argBucketCacheExpiryMin),
		BucketFilters						(argBucketFilters),
		ClientGuid							(argClientGuid),
		DefaultCommonPrefixTime				(argDefaultCommonPrefixTime),
		IgnoreFileNamePatterns				(argIgnoreFileNamePatterns),
		MaxDisplayBuckets					(argMaxDisplayBuckets),
		MaxDisplayObjects					(argMaxDisplayObjects),
		ObjectCacheExpiryMin				(argObjectCacheExpiryMin),
		ClientRegion						(argClientRegion),
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
	const int								MaxDisplayBuckets;
	const int								MaxDisplayObjects;
	const int								ObjectCacheExpiryMin;
	const std::wstring						ClientRegion;
	const bool								StrictBucketRegion;
	const bool								StrictFileTimestamp;
	const int								TransferWriteSizeMib;

	WINCSESDKS3_API std::wstring str() const;
};

}	// namespace CSESS3

// EOF