#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WCSE {

struct ITask
{
	uint64_t mAddTime = 0ULL;
	wchar_t* mCaller = nullptr;

	virtual ~ITask()
	{
		delete mCaller;
	}

	virtual void run(CALLER_ARG0) = 0;
	virtual void cancelled(CALLER_ARG0) noexcept { }
};

struct IWorker : public ICSService
{
	virtual ~IWorker() = default;
	virtual bool addTask(CALLER_ARG ITask* argTask) = 0;
};

struct IOnDemandTask : public ITask
{
	enum class IgnoreDuplicates
	{
		Yes,
		No,
	};

	enum class Priority
	{
		High,
		Middle,
		Low,
	};

	virtual IgnoreDuplicates getIgnoreDuplicates() const noexcept = 0;
	virtual Priority getPriority() const noexcept = 0;
	virtual std::wstring synonymString() const noexcept { return std::wstring{}; }
};

struct IScheduledTask : public ITask
{
	virtual bool shouldRun(int i) const noexcept = 0;
};

template<typename T>
struct ITaskTypedWorker : public IWorker
{
	virtual bool addTask(CALLER_ARG ITask* argTask) override
	{
		T* task = dynamic_cast<T*>(argTask);
		return task ? addTypedTask(CONT_CALLER task) : false;
	}

	virtual bool addTypedTask(CALLER_ARG T* argTask) = 0;
};

typedef struct
{
	const wchar_t* name;
	IWorker* worker;
}
NamedWorker;

} // namespace WCSE

#pragma warning(pop)
// EOF