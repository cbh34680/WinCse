#pragma once

#include <string>

namespace WinCseLib {

enum CanIgnore
{
	YES,
	NO
};

enum Priority
{
	LOW,
	HIGH
};


struct IWorker;

struct WINCSELIB_API ITask
{
	bool mPriority = Priority::LOW;
	int _mWorkerId_4debug = -1;

	virtual ~ITask() = default;

	virtual std::wstring synonymString();

	virtual void run(CALLER_ARG IWorker* worker, const int indent) = 0;
};

struct WINCSELIB_API IWorker : public IService
{
	virtual ~IWorker() = default;

	virtual bool addTask(ITask* pTask, CanIgnore ignState, Priority priority) = 0;
};

} // namespace WinCseLib

// EOF