#pragma once

#include <queue>
#include <thread>

class DelayedWorker : public IWorker
{
private:
	const wchar_t* mTempDir;
	const wchar_t* mIniSection;
	std::vector<std::thread> mThreads;
	std::deque<std::shared_ptr<ITask>> mTaskQueue;

	HANDLE mEvent = nullptr;

protected:
	void listenEvent(const int i);
	std::shared_ptr<ITask> dequeueTask();

public:
	DelayedWorker(const wchar_t* tmpdir, const wchar_t* iniSection);
	~DelayedWorker();

	bool OnSvcStart(const wchar_t* WorkDir) override;
	void OnSvcStop() override;

	bool addTask(ITask* task, CanIgnore ignState, Priority priority) override;
};

// EOF