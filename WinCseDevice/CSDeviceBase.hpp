#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "QueryBucket.hpp"
#include "QueryObject.hpp"
#include "IApiClient.hpp"

namespace CSEDVC
{

class CSDeviceBase : public CSELIB::ICSDevice
{
private:
	const std::map<std::wstring, CSELIB::IWorker*>	mWorkers;

protected:
	const std::wstring				mIniSection;
	std::unique_ptr<RuntimeEnv>		mRuntimeEnv;
	std::unique_ptr<IApiClient>		mApiClient;
	std::unique_ptr<QueryBucket>	mQueryBucket;
	std::unique_ptr<QueryObject>	mQueryObject;

public:
	WINCSEDEVICE_API void onTimer();
	WINCSEDEVICE_API void onIdle();
	WINCSEDEVICE_API bool onNotif(const std::wstring& argNotifName) override;

protected:
	CSELIB::IWorker* getWorker(const std::wstring& argName) const
	{
		return mWorkers.at(argName);
	}

	virtual IApiClient* newApiClient(RuntimeEnv* argRuntimeEnv, CSELIB::IWorker* argDelayedWorker) = 0;

	virtual QueryBucket* newQueryBucket(RuntimeEnv* argRuntimeEnv, IApiClient* argApiClient)
	{
		return new QueryBucket(argRuntimeEnv, argApiClient);
	}

	virtual QueryObject* newQueryObject(RuntimeEnv* argRuntimeEnv, IApiClient* argApiClient)
	{
		return new QueryObject(argRuntimeEnv, argApiClient);
	}

public:
	CSDeviceBase(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers)
		:
		mIniSection(argIniSection),
		mWorkers(argWorkers)
	{
	}

	std::list<std::wstring> getNotificationList() override
	{
		return { L"Global\\WinCse-util-sdk-s3-clear-cache" };
	}

	WINCSEDEVICE_API NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;

	bool shouldIgnoreWinPath(const std::filesystem::path& argWinPath) override
	{
		APP_ASSERT(!argWinPath.empty());
		APP_ASSERT(argWinPath.wstring().at(0) == L'\\');

		return mRuntimeEnv->shouldIgnoreWinPath(argWinPath);
	}

	WINCSEDEVICE_API void printReport(FILE* fp) override;
};

}	// namespace CSEDVC

// EOF