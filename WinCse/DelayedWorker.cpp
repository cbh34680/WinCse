#include "DelayedWorker.hpp"

using namespace CSELIB;
using namespace CSEDRV;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// タスク処理が有効
const int WORKER_MAX = 6;

#else
// タスク処理が無効
const int WORKER_MAX = 0;

#endif

DelayedWorker::DelayedWorker(const std::wstring& argIniSection)
	:
	mIniSection(argIniSection)
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
		auto& thr = mThreads.emplace_back(&DelayedWorker::listen, this, i);

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

void DelayedWorker::listen(int argThreadIndex) noexcept
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

					traceW(L"(%d): wait for signal: error code=%lu, break", argThreadIndex, reason);

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
				task->run(argThreadIndex);
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
		task->cancelled();
	}

	traceW(L"(%d): exit event loop", argThreadIndex);
}

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//

#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ mGuard }

#if ENABLE_TASK
bool DelayedWorker::addTypedTask(CSELIB::IOnDemandTask* argTask)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask)

	if (mEndWorkerFlag)
	{
		// 念のため、ワーカー・スレッド停止後のリクエストは破棄
		// --> 基本的にはありえない

		argTask->cancelled();
		delete argTask;

		return false;
	}

	mTaskQueue.emplace_back(argTask);

	// WaitForSingleObject() に通知

	const auto b = ::SetEvent(mEvent.handle());
	APP_ASSERT(b);

	return true;
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

bool DelayedWorker::addTypedTask(CALLER_ARG CSELIB::IOnDemandTask* argTask)
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