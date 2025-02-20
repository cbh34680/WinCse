#pragma once

#include <queue>
#include <thread>

class IdleWorker : public IWorker
{
private:
	const wchar_t* mTempDir;
	const wchar_t* mIniSection;
	std::vector<std::thread> mThreads;
	std::deque<std::shared_ptr<ITask>> mTasks;

	HANDLE mEvent = nullptr;

protected:
	void listenEvent(const int i);
	std::deque<std::shared_ptr<ITask>> getTasks();

public:
	IdleWorker(const wchar_t* tmpdir, const wchar_t* iniSection);
	~IdleWorker();

	bool OnSvcStart(const wchar_t* WorkDir) override;
	void OnSvcStop() override;

	bool addTask(ITask* task, CanIgnore ignState, Priority priority) override;
};

// EOF