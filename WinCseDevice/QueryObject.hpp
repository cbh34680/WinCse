#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "CacheObject.hpp"

namespace CSEDVC
{

class QueryObject final
{
private:
	const RuntimeEnv* const		mRuntimeEnv;
	IApiClient* const			mApiClient;
	CacheHeadObject				mCacheHeadObject;
	CacheListObjects			mCacheListObjects;

public:
	WINCSEDEVICE_API QueryObject(const RuntimeEnv* argRuntimeEnv, IApiClient* argApiClient)
		:
		mRuntimeEnv(argRuntimeEnv),
		mApiClient(argApiClient)
	{
	}

	WINCSEDEVICE_API bool qoHeadObjectFromCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) const;
	WINCSEDEVICE_API bool qoIsInNegativeCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const;
	WINCSEDEVICE_API void qoReportCache(CALLER_ARG FILE* fp) const;
	WINCSEDEVICE_API int qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	WINCSEDEVICE_API int qoClearCache(CALLER_ARG0);
	WINCSEDEVICE_API int qoDeleteCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey);

	WINCSEDEVICE_API bool qoHeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	WINCSEDEVICE_API bool qoHeadObjectOrListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	WINCSEDEVICE_API bool qoListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList);
};

}	// namespace CSEDVC

// EOF