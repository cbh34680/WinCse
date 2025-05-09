#pragma once

#include "CSDriverCommon.h"
#include <queue>

namespace CSEDRV
{

class DelayedWorker final : public CSELIB::ITaskTypedWorker<CSELIB::IOnDemandTask>
{
private:
	const std::wstring									mIniSection;
	std::list<std::thread>								mThreads;
	int													mTaskSkipCount = 0;
	std::atomic<bool>									mEndWorkerFlag = false;
	std::deque<std::unique_ptr<CSELIB::IOnDemandTask>>	mTaskQueue;
	CSELIB::EventHandle									mEvent;

	mutable std::mutex									mGuard;

protected:
	void listen(int argThreadIndex);
	std::unique_ptr<CSELIB::IOnDemandTask> dequeueTask();

public:
	DelayedWorker(const std::wstring& argIniSection);
	~DelayedWorker();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	bool addTypedTask(CSELIB::IOnDemandTask* argTask) override;
};

}	// namespace CSELIB

// EOF