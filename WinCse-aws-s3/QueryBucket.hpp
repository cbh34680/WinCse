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
	QueryBucket(const RuntimeEnv* argRuntimeEnv, ExecuteApi* argExecuteApi) noexcept
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	void qbClearCache(CALLER_ARG0) noexcept;
	void qbReportCache(CALLER_ARG FILE* fp) const noexcept;

	std::wstring qbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName) noexcept;
	bool qbHeadBucket(CALLER_ARG const std::wstring& bucketName, CSELIB::DirInfoPtr* pDirInfo) noexcept;
	bool qbListBuckets(CALLER_ARG CSELIB::DirInfoPtrList* pDirInfoList, const std::set<std::wstring>& options) noexcept;
	bool qbReload(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept;
};

}	// namespace CSEDAS3

// EOF