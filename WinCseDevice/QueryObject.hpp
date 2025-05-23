#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "CacheObject.hpp"
#include "IApiClient.hpp"

namespace CSEDVC
{

class QueryObject
{
protected:
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

	virtual ~QueryObject() = default;

	WINCSEDEVICE_API virtual bool qoHeadObjectFromCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) const;
	WINCSEDEVICE_API virtual bool qoIsInNegativeCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const;
	WINCSEDEVICE_API virtual void qoReportCache(CALLER_ARG FILE* fp) const;
	WINCSEDEVICE_API virtual int qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	WINCSEDEVICE_API virtual int qoClearCache(CALLER_ARG0);
	WINCSEDEVICE_API virtual int qoDeleteCache(CALLER_ARG const CSELIB::ObjectKey& argObjKey);
	WINCSEDEVICE_API virtual bool qoHeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	WINCSEDEVICE_API virtual bool qoHeadObjectOrListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);
	WINCSEDEVICE_API virtual bool qoListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList);
};

}	// namespace CSEDVC

// EOF