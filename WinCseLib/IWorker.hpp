#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WinCseLib {

enum class CanIgnore
{
	None,
	Yes,
	No,
};

enum class Priority
{
	Low,
	High,
};


struct IWorker;

// ���ꂾ�� C4251 �̌x�����o��̂ŁA�����o�ɒ��ڏC������
//struct WINCSELIB_API ITask

struct ITask
{
	Priority mPriority = Priority::Low;
	int mWorkerId_4debug = -1;
	std::wstring mCaller_4debug;

	WINCSELIB_API virtual ~ITask() = default;

	WINCSELIB_API virtual std::wstring synonymString() { return std::wstring(L""); }

	WINCSELIB_API virtual void run(CALLER_ARG0) = 0;
};

struct WINCSELIB_API IWorker : public ICSService
{
	virtual ~IWorker() = default;

	virtual bool addTask(CALLER_ARG ITask* pTask, Priority priority, CanIgnore ignState)
	{
		delete pTask;
		return false;
	}
};

} // namespace WinCseLib

#pragma warning(pop)
// EOF