#pragma once

#include <thread>

class DelayedWorker : public WinCseLib::IWorker
{
private:
	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::list<std::thread> mThreads;
	int mTaskSkipCount;

	HANDLE mEvent = NULL;

protected:
	void listenEvent(const int i);
	std::unique_ptr<WinCseLib::ITask> dequeueTask();

public:
	DelayedWorker(const std::wstring& argTempDir, const std::wstring& argIniSection);
	~DelayedWorker();

	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	bool addTask(CALLER_ARG WinCseLib::ITask* argTask, WinCseLib::Priority priority, WinCseLib::CanIgnore ignState) override;
};

// EOF