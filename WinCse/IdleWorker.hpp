#pragma once

#include <queue>
#include <thread>

class IdleWorker : public WinCseLib::IWorker
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::vector<std::thread> mThreads;
	std::deque<std::shared_ptr<WinCseLib::ITask>> mTasks;

	HANDLE mEvent = nullptr;

protected:
	void listenEvent(const int i);
	std::deque<std::shared_ptr<WinCseLib::ITask>> getTasks();

public:
	IdleWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	~IdleWorker();

	bool OnSvcStart(const wchar_t* WorkDir) override;
	void OnSvcStop() override;

	bool addTask(WinCseLib::ITask* task, WinCseLib::CanIgnore ignState, WinCseLib::Priority priority) override;
};

// EOF