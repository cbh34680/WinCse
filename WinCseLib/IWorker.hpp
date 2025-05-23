#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace CSELIB {

struct ITask
{
public:
	virtual ~ITask() = default;

	virtual void run(int argThreadIndex) = 0;
	virtual void cancelled() { }
};

struct IWorker : public ICSService
{
	virtual bool addTask(ITask* argTask) = 0;
};

struct IOnDemandTask : public ITask
{
};

struct IScheduledTask : public ITask
{
	virtual bool shouldRun(int argTick) const = 0;
};

template<typename T>
struct ITaskTypedWorker : public IWorker
{
	bool addTask(ITask* argTask) override
	{
		T* task = dynamic_cast<T*>(argTask);
		APP_ASSERT(task);

		return task ? this->addTypedTask(task) : false;
	}

	virtual bool addTypedTask(T* argTask) = 0;
};

typedef struct
{
	PCWSTR		name;
	IWorker*	worker;
}
NamedWorker;

}	// namespace CSELIB

#pragma warning(pop)
// EOF