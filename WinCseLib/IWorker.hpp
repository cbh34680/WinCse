#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WinCseLib {

enum class CanIgnoreDuplicates
{
	None,
	Yes,
	No,
};

enum class Priority
{
	High,
	Middle,
	Low,
};


struct IWorker;

struct WINCSELIB_API ITask
{
	Priority mPriority = Priority::Low;
	uint64_t mAddTime = 0ULL;
	wchar_t* mCaller = nullptr;

	virtual ~ITask()
	{
		delete mCaller;
	}

	virtual std::wstring synonymString() { return std::wstring{}; }

	virtual void run(CALLER_ARG0) = 0;
	virtual void cancelled(CALLER_ARG0) { }
};

struct WINCSELIB_API IWorker : public ICSService
{
	virtual ~IWorker() = default;

	virtual bool addTask(CALLER_ARG ITask* pTask, Priority priority, CanIgnoreDuplicates ignState)
	{
		pTask->cancelled(CONT_CALLER0);
		delete pTask;
		return false;
	}
};

} // namespace WinCseLib

#pragma warning(pop)
// EOF