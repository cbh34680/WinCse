#pragma once

#include "SdkS3Common.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "CacheListBuckets.hpp"

namespace CSESS3
{

class QueryBucket final
{
private:
	const RuntimeEnv* const		mRuntimeEnv;
	ExecuteApi* const			mExecuteApi;
	CacheListBuckets			mCacheListBuckets;

public:
	WINCSESDKS3_API QueryBucket(const RuntimeEnv* argRuntimeEnv, ExecuteApi* argExecuteApi)
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	WINCSESDKS3_API void qbClearCache(CALLER_ARG0);
	WINCSESDKS3_API void qbReportCache(CALLER_ARG FILE* fp) const;

	WINCSESDKS3_API std::wstring qbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName);
	WINCSESDKS3_API bool qbHeadBucket(CALLER_ARG const std::wstring& bucketName, CSELIB::DirEntryType* pDirEntry);
	WINCSESDKS3_API bool qbListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList);
	WINCSESDKS3_API bool qbReload(CALLER_ARG std::chrono::system_clock::time_point threshold);
};

}	// namespace CSEAS3

// EOF