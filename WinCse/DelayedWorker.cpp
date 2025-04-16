#include "DelayedWorker.hpp"
#include <filesystem>

using namespace WCSE;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// タスク処理が有効
const int WORKER_MAX = 6;

#else
// タスク処理が無効
const int WORKER_MAX = 0;

#endif

DelayedWorker::DelayedWorker(const std::wstring&, const std::wstring& argIniSection)
	:
	mIniSection(argIniSection),
	mTaskSkipCount(0)
{
	// OnSvcStart の呼び出し順によるイベントオブジェクト未生成を
	// 回避するため、コンストラクタで生成して OnSvcStart で null チェックする

	mEvent = ::CreateEventW(NULL, FALSE, FALSE, NULL);
	APP_ASSERT(mEvent.valid());
}

DelayedWorker::~DelayedWorker()
{
	NEW_LOG_BLOCK();

	this->OnSvcStop();

	traceW(L"close event");
	mEvent.close();
	traceW(L"close event done");
}

NTSTATUS DelayedWorker::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM*)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);

	if (mEvent.invalid())
	{
		traceW(L"mEvent is null");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for (int i=0; i<WORKER_MAX; i++)
	{
		auto& thr = mThreads.emplace_back(&DelayedWorker::listenEvent, this, i);

		std::wostringstream ss;
		ss << L"WinCse::DelayedWorker ";
		ss << i;

		auto h = thr.native_handle();
		const auto hresult = ::SetThreadDescription(h, ss.str().c_str());
		APP_ASSERT(SUCCEEDED(hresult));
		
		//BOOL b = ::SetThreadPriority(h, THREAD_PRIORITY_HIGHEST);
		BOOL b = ::SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
		APP_ASSERT(b);
	}

	return STATUS_SUCCESS;
}

VOID DelayedWorker::OnSvcStop()
{
	NEW_LOG_BLOCK();

	// デストラクタからも呼ばれるので、再入可能としておくこと

	if (!mThreads.empty())
	{
		traceW(L"wait for thread end ...");

		mEndWorkerFlag = true;

		for (int i=0; i<mThreads.size(); i++)
		{
			const auto b = ::SetEvent(mEvent.handle());
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

void DelayedWorker::listenEvent(const int argThreadIndex) noexcept
{
	NEW_LOG_BLOCK();

	while (1)
	{
		//traceW(L"(%d): wait for signal ...", argThreadIndex);
		const auto reason = ::WaitForSingleObject(mEvent.handle(), INFINITE);

		bool breakLoop = false;

		if (mEndWorkerFlag)
		{
			traceW(L"(%d): receive end worker request", argThreadIndex);

			breakLoop = true;
		}
		else
		{
			switch (reason)
			{
				case WAIT_OBJECT_0:
				{
					// SetEvent の実行

					//traceW(L"(%d): wait for signal: catch signal", argThreadIndex);
					break;
				}

				default:
				{
					// タイムアウト、又はシステムエラー

					traceW(L"(%d): wait for signal: error code=%ld, break", argThreadIndex, reason);

					breakLoop = true;

					break;
				}
			}
		}

		if (breakLoop)
		{
			traceW(L"(%d): catch end-loop request, break", argThreadIndex);
			break;
		}

		// キューに入っているタスクを処理

		while (1)
		{
			auto task{ dequeueTask() };
			if (!task)
			{
				//traceW(L"(%d): no more oneshot-tasks", argThreadIndex);
				break;
			}

			try
			{
				//traceW(L"(%d): run oneshot task ...", argThreadIndex);
				task->run(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
				//traceW(L"(%d): run oneshot task done", argThreadIndex);
			}
			catch (const std::exception& ex)
			{
				traceA("(%d): what: %s", argThreadIndex, ex.what());
				break;
			}
			catch (...)
			{
				traceA("(%d): unknown error, continue", argThreadIndex);
			}
		}
	}

	// 残ったタスクはキャンセルして破棄

	while (1)
	{
		auto task{ dequeueTask() };
		if (!task)
		{
			traceW(L"(%d): no more oneshot-tasks", argThreadIndex);
			break;
		}

		const auto klassName{ getDerivedClassNamesW(task.get()) };

		traceW(L"cancel task=%s", klassName.c_str());
		task->cancelled(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
	}

	traceW(L"(%d): exit event loop", argThreadIndex);
}

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//

static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }

bool taskComparator(const std::unique_ptr<IOnDemandTask>& a, const std::unique_ptr<IOnDemandTask>& b)
{
	if (a->getPriority() < b->getPriority())
	{
		return true;
	}
	else if (a->getPriority() > b->getPriority())
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

#if ENABLE_TASK
bool DelayedWorker::addTypedTask(CALLER_ARG WCSE::IOnDemandTask* argTask)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask)

	if (mEndWorkerFlag)
	{
		// 念のため、ワーカー・スレッド停止後のリクエストは破棄
		// --> 基本的にはありえない

		argTask->cancelled(CONT_CALLER0);
		delete argTask;

		return false;
	}

	bool add = false;

	argTask->mAddTime = GetCurrentUtcMillis();
	argTask->mCaller = _wcsdup(CALL_CHAIN().c_str());

	if (argTask->getIgnoreDuplicates() == IOnDemandTask::IgnoreDuplicates::Yes)
	{
		const auto taskName{ argTask->synonymString() };

		// 無視可能の時には synonym は設定されるべき

		APP_ASSERT(!taskName.empty());

		// キューから同じシノニムを持つタスクを探す

		const auto it = std::find_if(mTaskQueue.cbegin(), mTaskQueue.cend(), [&taskName](const auto& task)
		{
			return task->synonymString() == taskName;
		});

		if (it == mTaskQueue.cend())
		{
			// 同等のものが存在しない

			add = true;
		}
		else
		{
			const auto klassName{ getDerivedClassNamesW(argTask) };

			traceW(L"%s: %s: task ignored", klassName.c_str(), taskName.c_str());

			mTaskSkipCount++;
		}
	}
	else
	{
		// 無視できない

		add = true;
	}

	if (add)
	{
		mTaskQueue.emplace_back(argTask);
	}

	if (add)
	{
		// Priority, AddTime の順にソート

		std::sort(mTaskQueue.begin(), mTaskQueue.end(), taskComparator);

		// WaitForSingleObject() に通知

		const auto b = ::SetEvent(mEvent.handle());
		APP_ASSERT(b);
	}
	else
	{
		argTask->cancelled(CONT_CALLER0);
		delete argTask;
	}

	return add;
}

std::unique_ptr<IOnDemandTask> DelayedWorker::dequeueTask() noexcept
{
	THREAD_SAFE();

	if (!mTaskQueue.empty())
	{
		auto ret{ std::move(mTaskQueue.front()) };
		mTaskQueue.pop_front();

		return ret;
	}

	return nullptr;
}

#else

bool DelayedWorker::addTypedTask(CALLER_ARG WCSE::IOnDemandTask* argTask)
{
	THREAD_SAFE();

	// ワーカー処理が無効な場合は、タスクのリクエストを無視

	argTask->cancelled(CONT_CALLER0);
	delete argTask;

	return add;
}

std::unique_ptr<IOnDemandTask> DelayedWorker::dequeueTask() noexcept
{
	THREAD_SAFE();

	// ワーカー処理が無効な場合は、null を返却

	return nullptr;
}
#endif

// EOF