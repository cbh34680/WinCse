#pragma once

#include <thread>
#include <atomic>
#include <queue>

class DelayedWorker : public WCSE::ITaskTypedWorker<WCSE::IOnDemandTask>
{
private:
	const std::wstring mIniSection;

	std::list<std::thread> mThreads;
	int mTaskSkipCount;
	std::deque<std::unique_ptr<WCSE::IOnDemandTask>> mTaskQueue;
	std::atomic<bool> mEndWorkerFlag = false;
	WCSE::EventHandle mEvent;

protected:
	void listenEvent(const int argThreadIndex) noexcept;
	std::unique_ptr<WCSE::IOnDemandTask> dequeueTask() noexcept;

public:
	DelayedWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	~DelayedWorker();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	bool addTypedTask(CALLER_ARG WCSE::IOnDemandTask* argTask) override;
};

// EOF