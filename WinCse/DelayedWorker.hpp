#pragma once

#include <thread>
#include <atomic>
#include <queue>

class DelayedWorker : public WinCseLib::ITaskTypedWorker<WinCseLib::IOnDemandTask>
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::list<std::thread> mThreads;
	int mTaskSkipCount;
	std::deque<std::unique_ptr<WinCseLib::IOnDemandTask>> mTaskQueue;
	std::atomic<bool> mEndWorkerFlag = false;

	WinCseLib::EventHandle mEvent;

protected:
	void listenEvent(const int i);
	std::unique_ptr<WinCseLib::IOnDemandTask> dequeueTask();

public:
	DelayedWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	~DelayedWorker();

	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	bool addTypedTask(CALLER_ARG WinCseLib::IOnDemandTask* argTask) override;
};

// EOF