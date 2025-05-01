#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "CacheObject.hpp"

namespace CSEDAS3
{

class QueryObject final
{
private:
	const RuntimeEnv* const	mRuntimeEnv;
	ExecuteApi* const		mExecuteApi;
	CacheHeadObject			mCacheHeadObject;
	CacheListObjects		mCacheListObjects;

public:
	QueryObject(const RuntimeEnv* argRuntimeEnv, ExecuteApi* argExecuteApi) noexcept
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	bool qoHeadObjectFromCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtr* pDirInfo) const noexcept;
	bool qoIsInNegativeCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const noexcept;
	void qoReportCache(CALLER_ARG FILE* fp) const noexcept;
	int qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept;
	int qoClearCache(CALLER_ARG0) noexcept;
	int qoDeleteCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey) noexcept;

	bool qoHeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtr* pDirInfo) noexcept;
	bool qoHeadObjectOrListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtr* pDirInfo) noexcept;
	bool qoListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtrList* pDirInfoList) noexcept;
};

}	// namespace CSEDAS3

// EOF