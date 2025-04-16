#pragma once

#include "WinCseLib.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "CacheObject.hpp"

class QueryObject
{
private:
	const RuntimeEnv* const mRuntimeEnv;
	ExecuteApi* const mExecuteApi;

	CacheHeadObject mCacheHeadObject;
	CacheListObjects mCacheListObjects;

public:
	QueryObject(const RuntimeEnv* argRuntimeEnv, ExecuteApi* argExecuteApi) noexcept
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	bool headObjectFromCache(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pDirInfo) const noexcept;
	bool isNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey) const noexcept;
	void reportCache(CALLER_ARG FILE* fp) const noexcept;
	int deleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept;
	int clearCache(CALLER_ARG0) noexcept;
	int deleteCache(CALLER_ARG const WCSE::ObjectKey& argObjKey) noexcept;

	bool unsafeHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pDirInfo) noexcept;
	bool unsafeHeadObject_CheckDir(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pDirInfo) noexcept;
	bool unsafeListObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoListType* pDirInfoList) noexcept;
};

// EOF