#pragma once

#include <thread>
#include <atomic>
#include <queue>

class DelayedWorker : public WCSE::ITaskTypedWorker<WCSE::IOnDemandTask>
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::list<std::thread> mThreads;
	int mTaskSkipCount;
	std::deque<std::unique_ptr<WCSE::IOnDemandTask>> mTaskQueue;
	std::atomic<bool> mEndWorkerFlag = false;

	WCSE::EventHandle mEvent;

protected:
	void listenEvent(const int i);
	std::unique_ptr<WCSE::IOnDemandTask> dequeueTask();

public:
	DelayedWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	~DelayedWorker();

	bool OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem, PCWSTR PtfsPath) override;
	void OnSvcStop() override;

	bool addTypedTask(CALLER_ARG WCSE::IOnDemandTask* argTask) override;
};

// EOF