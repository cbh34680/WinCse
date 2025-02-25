#pragma once

#include <queue>
#include <thread>

class DelayedWorker : public WinCseLib::IWorker
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::vector<std::thread> mThreads;
	std::deque<std::shared_ptr<WinCseLib::ITask>> mTaskQueue;
	int mTaskSkipCount;

	HANDLE mEvent = nullptr;

protected:
	void listenEvent(const int i);
	std::shared_ptr<WinCseLib::ITask> dequeueTask();

public:
	DelayedWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	~DelayedWorker();

	bool OnSvcStart(const wchar_t* WorkDir) override;
	void OnSvcStop() override;

	bool addTask(WinCseLib::ITask* task, WinCseLib::CanIgnore ignState, WinCseLib::Priority priority) override;
};

// EOF