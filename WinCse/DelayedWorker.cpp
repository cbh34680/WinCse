#include "WinCseLib.h"
#include "DelayedWorker.hpp"
#include <filesystem>
#include <mutex>
#include <sstream>

using namespace WinCseLib;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// タスク処理が有効
const int WORKER_MAX = 2;

#else
// タスク処理が無効
const int WORKER_MAX = 0;

#endif

DelayedWorker::DelayedWorker(const std::wstring& tmpdir, const std::wstring& iniSection)
	: mTempDir(tmpdir), mIniSection(iniSection), mTaskSkipCount(0)
{
	// OnSvcStart の呼び出し順によるイベントオブジェクト未生成を
	// 回避するため、コンストラクタで生成して OnSvcStart で null チェックする

	mEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
}

DelayedWorker::~DelayedWorker()
{
	NEW_LOG_BLOCK();

	if (mEvent)
	{
		traceW(L"close event");
		::CloseHandle(mEvent);
	}
}

bool DelayedWorker::OnSvcStart(const wchar_t* argWorkDir)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);

	if (!mEvent)
	{
		traceW(L"mEvent is null");
		return false;
	}

	for (int i=0; i<WORKER_MAX; i++)
	{
		mThreads.emplace_back(&DelayedWorker::listenEvent, this, i);
	}

	return true;
}

struct BreakLoopRequest : public std::exception
{
	BreakLoopRequest(char const* const msg) : std::exception(msg) { }
};

struct BreakLoopTask : public ITask
{
	void run(CALLER_ARG IWorker* worker, const int indent) override
	{
		GetLogger()->traceW_impl(indent, __FUNCTIONW__, __LINE__, __FUNCTIONW__, L"throw break");

		throw BreakLoopRequest("from " __FUNCTION__);
	}
};

void DelayedWorker::OnSvcStop()
{
	NEW_LOG_BLOCK();

	traceW(L"wait for thread end ...");

	for (int i=0; i<mThreads.size(); i++)
	{
		// 最優先の停止命令
		addTask(new BreakLoopTask, CanIgnore::NO, Priority::HIGH);
	}

	for (auto& thr: mThreads)
	{
		thr.join();
	}

	traceW(L"done.");
}

void DelayedWorker::listenEvent(const int i)
{
	NEW_LOG_BLOCK();

	while (1)
	{
		try
		{
			traceW(L"(%d): wait for signal ...", i);
			const auto reason = ::WaitForSingleObject(mEvent, INFINITE);

			switch (reason)
			{
				case WAIT_TIMEOUT:
				{
					APP_ASSERT(0);

					break;
				}

				case WAIT_OBJECT_0:
				{
					traceW(L"(%d): wait for signal: catch signal", i);
					break;
				}

				default:
				{
					traceW(L"(%d): wait for signal: error code=%ld, continue", i, reason);
					throw std::runtime_error("illegal route");

					break;
				}
			}

			// キューに入っているタスクを処理
			while (1)
			{
				auto task{ dequeueTask() };
				if (!task)
				{
					traceW(L"(%d): no more oneshot-tasks", i);
					break;
				}

				traceW(L"(%d): run oneshot task ...", i);
				task->_mWorkerId_4debug = i;
				task->run(INIT_CALLER this, LOG_DEPTH());
				traceW(L"(%d): run oneshot task done", i);

				// 処理するごとに他のスレッドに回す
				//::SwitchToThread();
			}
		}
		catch (const BreakLoopRequest&)
		{
			traceW(L"(%d): catch loop-break request, go exit thread", i);
			break;
		}
		catch (const std::runtime_error& err)
		{
			traceW(L"(%d): what: %s", i, err.what());
			break;
		}
		catch (...)
		{
			traceW(L"(%d): unknown error, continue", i);
		}
	}

	traceW(L"(%d): exit event loop", i);
}

static std::mutex gGuard;

#define THREAD_SAFE() \
    std::lock_guard<std::mutex> lock_(gGuard)


bool DelayedWorker::addTask(ITask* argTask, CanIgnore ignState, Priority priority)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask)

	bool added = false;

#if ENABLE_TASK
	argTask->mPriority = priority;

	if (priority == Priority::HIGH)
	{
		// 優先する場合は先頭に追加

		mTaskQueue.emplace_front(argTask);

		added = true;
	}
	else
	{
		// 通常は後方に追加
		const auto argTaskName{ argTask->synonymString() };

		if (ignState == CanIgnore::YES)
		{
			// 無視可能
			const auto it = std::find_if(mTaskQueue.begin(), mTaskQueue.end(), [&argTaskName](const auto& task)
			{
				// キューから同じシノニムを探す
				return task->synonymString() == argTaskName;
			});

			if (it == mTaskQueue.end())
			{
				// 同等のものが存在しない
				added = true;
			}
			else
			{
				// skip ??
				mTaskSkipCount++;
			}
		}
		else
		{
			// 無視できない
			added = true;
		}

		if (added)
		{
			// 後方に追加
			mTaskQueue.emplace_back(argTask);

			traceW(L"[%s]: task added", argTaskName.c_str());
		}
		else
		{
			delete argTask;

			traceW(L"[%s]: task ignored", argTaskName.c_str());
		}
	}

	if (added)
	{
		// WaitForSingleObject() に通知
		::SetEvent(mEvent);
	}

#else
	// ワーカー処理が無効な場合は、タスクのリクエストを無視
	delete argTask;

#endif

	return added;
}

std::shared_ptr<ITask> DelayedWorker::dequeueTask()
{
	THREAD_SAFE();
	//NEW_LOG_BLOCK();

#if ENABLE_TASK
	if (!mTaskQueue.empty())
	{
		auto ret{ mTaskQueue.front() };
		mTaskQueue.pop_front();

		return ret;
	}

#else
	// ワーカー処理が無効な場合は、null を返却

#endif

	return nullptr;
}

// EOF