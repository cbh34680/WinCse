#pragma once

#include <thread>
#include <queue>
#include <atomic>

class ScheduledWorker : public WinCseLib::ITaskTypedWorker<WinCseLib::IScheduledTask>
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::list<std::thread> mThreads;
	std::atomic<bool> mEndWorkerFlag = false;
	std::deque<std::shared_ptr<WinCseLib::IScheduledTask>> mTasks;

	WinCseLib::EventHandle mEvent;

protected:
	void listenEvent(const int i);
	std::deque<std::shared_ptr<WinCseLib::IScheduledTask>> getTasks();

	virtual int getThreadPriority() const noexcept = 0;
	virtual DWORD getTimePeriodMillis() const noexcept = 0;

public:
	ScheduledWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	virtual ~ScheduledWorker();

	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	bool addTypedTask(CALLER_ARG WinCseLib::IScheduledTask* argTask) override;
};

// EOF