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
	QueryObject(const RuntimeEnv* argRuntimeEnv, ExecuteApi* argExecuteApi)
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	bool qoHeadObjectFromCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) const;
	bool qoIsInNegativeCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const;
	void qoReportCache(CALLER_ARG FILE* fp) const;
	int qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	int qoClearCache(CALLER_ARG0);
	int qoDeleteCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey);

	bool qoHeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	bool qoHeadObjectOrListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	bool qoListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList);
};

}	// namespace CSEDAS3

// EOF