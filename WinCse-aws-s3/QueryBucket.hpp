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

	void clearCache(CALLER_ARG0) noexcept;
	void reportCache(CALLER_ARG FILE* fp) const noexcept;

	std::wstring unsafeGetBucketRegion(CALLER_ARG const std::wstring& argBucketName) noexcept;
	bool unsafeHeadBucket(CALLER_ARG const std::wstring& bucketName, WCSE::DirInfoType* pDirInfo) noexcept;
	bool unsafeListBuckets(CALLER_ARG
		WCSE::DirInfoListType* pDirInfoList, const std::vector<std::wstring>& options) noexcept;
	bool unsafeReload(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept;
};

// EOF