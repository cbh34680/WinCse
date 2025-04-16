#pragma once

#include "WinCseLib.h"
#include <queue>

class ScheduledWorker : public WCSE::ITaskTypedWorker<WCSE::IScheduledTask>
{
private:
	const std::wstring mIniSection;

	std::list<std::thread> mThreads;
	std::atomic<bool> mEndWorkerFlag;
	std::deque<std::shared_ptr<WCSE::IScheduledTask>> mTasks;
	WCSE::EventHandle mEvent;

protected:
	void listenEvent(const int i) noexcept;
	std::deque<std::shared_ptr<WCSE::IScheduledTask>> getTasks() const noexcept;

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