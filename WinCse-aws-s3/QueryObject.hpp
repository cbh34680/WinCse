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

	WCSE::DirInfoType headObjectCacheOnly(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool isNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	void reportCache(CALLER_ARG FILE* fp);
	int deleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	int clearCache(CALLER_ARG0);
	int deleteCache(CALLER_ARG const WCSE::ObjectKey& argObjKey);

	WCSE::DirInfoType unsafeHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	WCSE::DirInfoType unsafeHeadObject_CheckDir(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool unsafeListObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList /* nullable */);
};

// EOF