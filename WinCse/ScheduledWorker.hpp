#pragma once

#include <thread>
#include <queue>
#include <atomic>

class ScheduledWorker : public WCSE::ITaskTypedWorker<WCSE::IScheduledTask>
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::list<std::thread> mThreads;
	std::atomic<bool> mEndWorkerFlag = false;
	std::deque<std::shared_ptr<WCSE::IScheduledTask>> mTasks;

	WCSE::EventHandle mEvent;

protected:
	void listenEvent(const int i);
	std::deque<std::shared_ptr<WCSE::IScheduledTask>> getTasks();

	virtual int getThreadPriority() const noexcept = 0;
	virtual DWORD getTimePeriodMillis() const noexcept = 0;

public:
	ScheduledWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	virtual ~ScheduledWorker();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	bool addTypedTask(CALLER_ARG WCSE::IScheduledTask* argTask) override;
};

// EOF