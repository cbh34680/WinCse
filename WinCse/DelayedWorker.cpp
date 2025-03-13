#include "WinCseLib.h"
#include "DelayedWorker.hpp"
#include <filesystem>
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
	APP_ASSERT(mEvent);
}

DelayedWorker::~DelayedWorker()
{
	NEW_LOG_BLOCK();

	this->OnSvcStop();

	traceW(L"close event");
	::CloseHandle(mEvent);
	traceW(L"close event done");
}

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
		auto& thr = mThreads.emplace_back(&DelayedWorker::listenEvent, this, i);

		std::wstringstream ss;
		ss << L"WinCse::DelayedWorker ";
		ss << i;

		auto h = thr.native_handle();
		::SetThreadDescription(h, ss.str().c_str());
		//::SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
	}

	return true;
}

void DelayedWorker::OnSvcStop()
{
	NEW_LOG_BLOCK();

	// デストラクタからも呼ばれるので、再入可能としておくこと

	if (!mThreads.empty())
	{
		traceW(L"wait for thread end ...");

		mEndWorkerFlag = true;

		for (int i=0; i<mThreads.size(); i++)
		{
			const auto b = ::SetEvent(mEvent);
			APP_ASSERT(b);
		}

		for (auto& thr: mThreads)
		{
			thr.join();
		}

		mThreads.clear();

		traceW(L"done.");
	}

	mTaskQueue.clear();
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

			if (mEndWorkerFlag)
			{
				traceW(L"(%d): receive end worker request", i);
				break;
			}

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
				task->run(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
				traceW(L"(%d): run oneshot task done", i);

				// 処理するごとに他のスレッドに回す
				//::SwitchToThread();
			}
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
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool taskComparator(const std::unique_ptr<ITask>& a, const std::unique_ptr<ITask>& b)
{
	if (a->mPriority < b->mPriority)
	{
		return true;
	}
	else if (a->mPriority > b->mPriority)
	{
		return false;
	}
	else
	{
		// a->mPriority == b->mPriority

		if (a->mAddTime < b->mAddTime)
		{
			return true;
		}
		else if (a->mAddTime > b->mAddTime)
		{
			return false;
		}

		// a->mAddTime == b->mAddTime
	}

	return false;
}

bool DelayedWorker::addTask(CALLER_ARG WinCseLib::ITask* argTask, WinCseLib::Priority priority, WinCseLib::CanIgnoreDuplicates ignState)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask)

	bool added = false;

#if ENABLE_TASK
	argTask->mPriority = priority;
	argTask->mAddTime = GetCurrentUtcMillis();
	argTask->mCaller = _wcsdup(CALL_CHAIN().c_str());

	if (priority == Priority::High)
	{
		// 優先する場合は先頭に追加

		mTaskQueue.emplace_front(argTask);

		added = true;
	}
	else
	{
		// 通常は後方に追加
		
		if (ignState == CanIgnoreDuplicates::Yes)
		{
			const auto argTaskName{ argTask->synonymString() };

			// キャンセル可能の時には synonym は設定されるべき
			APP_ASSERT(!argTaskName.empty());

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

			mTaskQueue.emplace_back(argTask);
		}
	}

	if (added)
	{
#if 1
		// Priority, AddTime の順にソート

		std::sort(mTaskQueue.begin(), mTaskQueue.end(), taskComparator);
#endif

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
	if (!mTaskQueue.empty())
	{
		auto ret{ std::move(mTaskQueue.front()) };
		mTaskQueue.pop_front();

		return ret;
	}

#else
	// ワーカー処理が無効な場合は、null を返却

#endif

	return nullptr;
}

// EOF