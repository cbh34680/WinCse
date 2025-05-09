#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "CacheListBuckets.hpp"

namespace CSEDAS3
{

class QueryBucket final
{
private:
	const RuntimeEnv* const	mRuntimeEnv;
	ExecuteApi* const		mExecuteApi;
	CacheListBuckets		mCacheListBuckets;

public:
	QueryBucket(const RuntimeEnv* argRuntimeEnv, ExecuteApi* argExecuteApi)
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	void qbClearCache(CALLER_ARG0);
	void qbReportCache(CALLER_ARG FILE* fp) const;

	std::wstring qbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName);
	bool qbHeadBucket(CALLER_ARG const std::wstring& bucketName, CSELIB::DirEntryType* pDirEntry);
	bool qbListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList, const std::set<std::wstring>& options);
	bool qbReload(CALLER_ARG std::chrono::system_clock::time_point threshold);
};

}	// namespace CSEDAS3

// EOF