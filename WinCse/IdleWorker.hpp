#pragma once

#include <thread>
#include <queue>
#include <atomic>

class IdleWorker : public WinCseLib::IWorker
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::list<std::thread> mThreads;
	std::atomic<bool> mEndWorkerFlag = false;
	std::deque<std::shared_ptr<WinCseLib::ITask>> mTasks;

	WinCseLib::EventHandle mEvent;

protected:
	void listenEvent(const int i);

	std::deque<std::shared_ptr<WinCseLib::ITask>> getTasks();

public:
	IdleWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	~IdleWorker();

	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	bool addTask(CALLER_ARG WinCseLib::ITask* argTask, WinCseLib::Priority priority, WinCseLib::CanIgnoreDuplicates ignState) override;
};

// EOF