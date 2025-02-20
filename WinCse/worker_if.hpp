#pragma once

#include <string>

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

struct ITask
{
	bool mPriority = Priority::LOW;
	int _mWorkerId_4debug = -1;

	virtual ~ITask() = 0;

	virtual std::wstring synonymString();

	virtual void run(CALLER_ARG IWorker* worker, const int indent) = 0;
};

struct IWorker : public IService
{
	virtual ~IWorker() = 0;

	virtual bool addTask(ITask* pTask, CanIgnore ignState, Priority priority) = 0;
};

// EOF