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
	None,
	High,
	Middle,
	Low,
};

struct IWorker;

struct WINCSELIB_API ITask
{
	uint64_t mAddTime = 0ULL;
	wchar_t* mCaller = nullptr;

	virtual ~ITask()
	{
		delete mCaller;
	}

	virtual CanIgnoreDuplicates getCanIgnoreDuplicates() const noexcept { return CanIgnoreDuplicates::None; }
	virtual Priority getPriority() const noexcept { return Priority::None; }
	virtual std::wstring synonymString() const noexcept { return std::wstring{}; }

	virtual void run(CALLER_ARG0) = 0;
	virtual void cancelled(CALLER_ARG0) { }
};

struct WINCSELIB_API IWorker : public ICSService
{
	virtual ~IWorker() = default;

	virtual bool addTask(CALLER_ARG ITask* pTask)
	{
		pTask->cancelled(CONT_CALLER0);
		delete pTask;
		return false;
	}
};

typedef struct
{
	const wchar_t* name;
	IWorker* worker;
}
NamedWorker;

} // namespace WinCseLib

#pragma warning(pop)
// EOF