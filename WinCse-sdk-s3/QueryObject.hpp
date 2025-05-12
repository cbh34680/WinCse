#pragma once

#include "SdkS3Common.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "CacheObject.hpp"

namespace CSESS3
{

class QueryObject final
{
private:
	const RuntimeEnv* const		mRuntimeEnv;
	ExecuteApi* const			mExecuteApi;
	CacheHeadObject				mCacheHeadObject;
	CacheListObjects			mCacheListObjects;

public:
	WINCSESDKS3_API QueryObject(const CSESS3::RuntimeEnv* argRuntimeEnv, CSESS3::ExecuteApi* argExecuteApi)
		:
		mRuntimeEnv(argRuntimeEnv),
		mExecuteApi(argExecuteApi)
	{
	}

	WINCSESDKS3_API bool qoHeadObjectFromCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) const;
	WINCSESDKS3_API bool qoIsInNegativeCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const;
	WINCSESDKS3_API void qoReportCache(CALLER_ARG FILE* fp) const;
	WINCSESDKS3_API int qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	WINCSESDKS3_API int qoClearCache(CALLER_ARG0);
	WINCSESDKS3_API int qoDeleteCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey);

	WINCSESDKS3_API bool qoHeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	WINCSESDKS3_API bool qoHeadObjectOrListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	WINCSESDKS3_API bool qoListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList);
};

}	// namespace CSEAS3

// EOF