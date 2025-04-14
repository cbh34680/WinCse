#pragma once

#include "WinCseLib.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "CacheListBuckets.hpp"

class QueryBucket
{
private:
	const RuntimeEnv* const mRuntimeEnv;
	ExecuteApi* const mExecuteApi;

	CacheListBuckets mCacheListBuckets;

public:
	QueryBucket(const RuntimeEnv* argRuntimeEnv, ExecuteApi* argExecuteApi) noexcept
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	void clearListBucketsCache(CALLER_ARG0);
	void reportListBucketsCache(CALLER_ARG FILE* fp);

	std::wstring unsafeGetBucketRegion(CALLER_ARG const std::wstring& argBucketName);
	WCSE::DirInfoType unsafeHeadBucket(CALLER_ARG const std::wstring& bucketName);
	bool unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */,
		const std::vector<std::wstring>& options);
	bool unsafeReloadListBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold);
};

// EOF