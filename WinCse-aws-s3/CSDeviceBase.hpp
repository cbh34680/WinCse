#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "QueryBucket.hpp"
#include "QueryObject.hpp"

namespace CSEDAS3
{

class CSDeviceBase : public CSELIB::ICSDevice
{
private:
	const std::wstring								mIniSection;
	const std::map<std::wstring, CSELIB::IWorker*>	mWorkers;

protected:
	std::unique_ptr<RuntimeEnv>						mRuntimeEnv;
	std::unique_ptr<ExecuteApi>						mExecuteApi;
	std::unique_ptr<QueryBucket>					mQueryBucket;
	std::unique_ptr<QueryObject>					mQueryObject;

public:
	void onTimer();
	void onIdle();
	bool onNotif(const std::wstring& argNotifName) override;

protected:
	CSELIB::IWorker* getWorker(const std::wstring& argName) const
	{
		return mWorkers.at(argName);
	}

public:
	explicit CSDeviceBase(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers);

	std::list<std::wstring> getNotificationList() override
	{
		return { L"Global\\WinCse-util-awss3-clear-cache" };
	}

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	bool shouldIgnoreFileName(const std::wstring& arg) override
	{
		return mExecuteApi->shouldIgnoreFileName(arg);
	}

	void printReport(FILE* fp) override;
};

}	// namespace CSEDAS3

// EOF