#include "WinCseLib.h"
#include "DelayedWorker.hpp"
#include <filesystem>
#include <queue>
#include <mutex>
#include <sstream>

using namespace WinCseLib;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// タスク処理が有効
const int WORKER_MAX = 4;

#else
// タスク処理が無効
const int WORKER_MAX = 0;

#endif

bool DelayedWorker::OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem)
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
	void run(CALLER_ARG0) override
	{
		NEW_LOG_BLOCK();

		traceW(L"throw break");

		throw BreakLoopRequest("from " __FUNCTION__);
	}
};

void DelayedWorker::OnSvcStop()
{
	NEW_LOG_BLOCK();

	// デストラクタからも呼ばれるので、再入可能としておくこと

	if (!mThreads.empty())
	{
		traceW(L"wait for thread end ...");

		for (int i=0; i<mThreads.size(); i++)
		{
			// 最優先の停止命令
			addTask(START_CALLER new BreakLoopTask, Priority::High, CanIgnore::No);
		}

		for (auto& thr: mThreads)
		{
			thr.join();
		}

		mThreads.clear();

		traceW(L"done.");
	}
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
				task->mWorkerId_4debug = i;
				task->run(task->mCaller_4debug + L"->" + __FUNCTIONW__);
				traceW(L"(%d): run oneshot task done", i);

				// 処理するごとに他のスレッドに回す
				//::SwitchToThread();
			}
		}
		catch (const BreakLoopRequest&)
		{
			traceA("(%d): catch loop-break request, go exit thread", i);
			break;
		}
		catch (const std::exception& err)
		{
			traceA("(%d): what: %s", i, err.what());
			break;
		}
		catch (...)
		{
			traceA("(%d): unknown error, continue", i);
		}
	}

	traceW(L"(%d): exit event loop", i);
}

//
// メンバに配置するとこのファイル以外からもアクセスできてしまう
// ことの回避処置。
// 
// static に置くとメモリリークとして認識されたり、いろいろ面倒なので
// コンストラクタとデストラクタで管理する
//
static struct LocalData
{
	std::mutex mGuard;
	std::deque<std::unique_ptr<WinCseLib::ITask>> mTaskQueue;
}
*Local;

DelayedWorker::DelayedWorker(const std::wstring& tmpdir, const std::wstring& iniSection)
	: mTempDir(tmpdir), mIniSection(iniSection), mTaskSkipCount(0)
{
	Local = new LocalData;
	APP_ASSERT(Local);

	// OnSvcStart の呼び出し順によるイベントオブジェクト未生成を
	// 回避するため、コンストラクタで生成して OnSvcStart で null チェックする

	mEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	APP_ASSERT(mEvent);
}

DelayedWorker::~DelayedWorker()
{
	NEW_LOG_BLOCK();

	this->OnSvcStop();

	traceW(L"close event");
	::CloseHandle(mEvent);

	delete Local;
}


//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(Local->mGuard)


bool DelayedWorker::addTask(CALLER_ARG WinCseLib::ITask* argTask, WinCseLib::Priority priority, WinCseLib::CanIgnore ignState)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask)

	bool added = false;

#if ENABLE_TASK
	argTask->mPriority = priority;
	argTask->mCaller_4debug = CALL_CHAIN();

	if (priority == Priority::High)
	{
		// 優先する場合は先頭に追加

		Local->mTaskQueue.emplace_front(argTask);

		added = true;
	}
	else
	{
		// 通常は後方に追加
		
		if (ignState == CanIgnore::Yes)
		{
			const auto argTaskName{ argTask->synonymString() };

			// キャンセル可能の時には synonym は設定されるべき
			APP_ASSERT(!argTaskName.empty());

			// 無視可能
			const auto it = std::find_if(Local->mTaskQueue.begin(), Local->mTaskQueue.end(), [&argTaskName](const auto& task)
			{
				// キューから同じシノニムを探す
				return task->synonymString() == argTaskName;
			});

			if (it == Local->mTaskQueue.end())
			{
				// 同等のものが存在しない
				added = true;
			}
			else
			{
				traceW(L"[%s]: task ignored", argTaskName.c_str());

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

			Local->mTaskQueue.emplace_back(argTask);
		}
	}

	if (added)
	{
		// WaitForSingleObject() に通知
		const auto b = ::SetEvent(mEvent);
		APP_ASSERT(b);
	}
	else
	{
		delete argTask;
	}

#else
	// ワーカー処理が無効な場合は、タスクのリクエストを無視
	delete argTask;

#endif

	return added;
}

std::unique_ptr<ITask> DelayedWorker::dequeueTask()
{
	THREAD_SAFE();
	//NEW_LOG_BLOCK();

#if ENABLE_TASK
	if (!Local->mTaskQueue.empty())
	{
		auto ret{ std::move(Local->mTaskQueue.front()) };
		Local->mTaskQueue.pop_front();

		return ret;
	}

#else
	// ワーカー処理が無効な場合は、null を返却

#endif

	return nullptr;
}

// EOF