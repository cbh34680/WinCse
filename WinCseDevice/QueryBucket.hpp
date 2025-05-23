#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "CacheListBuckets.hpp"
#include "IApiClient.hpp"

namespace CSEDVC
{

class QueryBucket
{
protected:
	const RuntimeEnv* const		mRuntimeEnv;
	IApiClient* const			mApiClient;
	CacheListBuckets			mCacheListBuckets;

public:
	WINCSEDEVICE_API QueryBucket(const RuntimeEnv* argRuntimeEnv, IApiClient* argApiClient)
		:
		mRuntimeEnv(argRuntimeEnv),
		mApiClient(argApiClient)
	{
	}

	virtual ~QueryBucket() = default;

	WINCSEDEVICE_API virtual void qbClearCache(CALLER_ARG0);
	WINCSEDEVICE_API virtual void qbReportCache(CALLER_ARG FILE* fp) const;
	WINCSEDEVICE_API virtual bool qbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion);
	WINCSEDEVICE_API virtual bool qbHeadBucket(CALLER_ARG const std::wstring& bucketName, CSELIB::DirEntryType* pDirEntry);
	WINCSEDEVICE_API virtual bool qbListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList);
	WINCSEDEVICE_API virtual bool qbReload(CALLER_ARG std::chrono::system_clock::time_point threshold);
};

}	// namespace CSEDVC

// EOF