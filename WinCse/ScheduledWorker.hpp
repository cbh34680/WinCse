#pragma once

#include "CSDriverCommon.h"
#include <queue>

namespace CSEDRV
{

class ScheduledWorker : public CSELIB::ITaskTypedWorker<CSELIB::IScheduledTask>
{
private:
	const std::wstring									mIniSection;
	std::list<std::thread>								mThreads;
	std::atomic<bool>									mEndWorkerFlag;
	std::deque<std::shared_ptr<CSELIB::IScheduledTask>>	mTasks;
	CSELIB::EventHandle									mEvent;
	mutable std::mutex									mGuard;

protected:
	void listen(int i) noexcept;
	std::deque<std::shared_ptr<CSELIB::IScheduledTask>> getTasks() const noexcept;

	virtual int getThreadPriority() const noexcept = 0;
	virtual DWORD getTimePeriodMillis() const noexcept = 0;

public:
	ScheduledWorker(const std::wstring& argIniSection);
	~ScheduledWorker();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	bool addTypedTask(CSELIB::IScheduledTask* argTask) override;
};

}	// namespace CSELIB

// EOF