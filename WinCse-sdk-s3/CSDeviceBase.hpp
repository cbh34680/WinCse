#pragma once

#include "SdkS3Common.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "QueryBucket.hpp"
#include "QueryObject.hpp"

namespace CSESS3
{

class CSDeviceBase : public CSELIB::ICSDevice
{
private:
	const std::map<std::wstring, CSELIB::IWorker*>	mWorkers;

protected:
	const std::wstring				mIniSection;
	std::unique_ptr<RuntimeEnv>		mRuntimeEnv;
	std::unique_ptr<ExecuteApi>		mExecuteApi;
	std::unique_ptr<QueryBucket>	mQueryBucket;
	std::unique_ptr<QueryObject>	mQueryObject;

public:
	WINCSESDKS3_API void onTimer();
	WINCSESDKS3_API void onIdle();
	WINCSESDKS3_API bool onNotif(const std::wstring& argNotifName) override;

protected:
	CSELIB::IWorker* getWorker(const std::wstring& argName) const
	{
		return mWorkers.at(argName);
	}

	virtual std::wstring getClientRegion() = 0;
	virtual Aws::S3::S3Client* getClient() = 0;

public:
	CSDeviceBase(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers)
		:
		mIniSection(argIniSection),
		mWorkers(argWorkers)
	{
	}

	std::list<std::wstring> getNotificationList() override
	{
		return { L"Global\\WinCse-util-awss3-clear-cache" };
	}

	WINCSESDKS3_API NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	WINCSESDKS3_API VOID OnSvcStop() override;

	bool shouldIgnoreFileName(const std::filesystem::path& argWinPath) override
	{
		APP_ASSERT(!argWinPath.empty());
		APP_ASSERT(argWinPath.wstring().at(0) == L'\\');

		return mExecuteApi->shouldIgnoreFileName(argWinPath);
	}

	WINCSESDKS3_API void printReport(FILE* fp) override;
};

}	// namespace CSESS3

// EOF