#include "ScheduledWorker.hpp"
#include <numeric>

using namespace CSELIB;

#define ENABLE_WORKER		(1)

#if ENABLE_WORKER
static const int WORKER_MAX = 1;
#else
static const int WORKER_MAX = 0;
#endif

namespace CSEDRV {

ScheduledWorker::ScheduledWorker(const std::wstring& argIniSection)
	:
	mIniSection(argIniSection)
{
	// OnSvcStart の呼び出し順によるイベントオブジェクト未生成を
	// 回避するため、コンストラクタで生成して OnSvcStart で null チェックする

	mEvent = ::CreateEventW(NULL, FALSE, FALSE, NULL);
	APP_ASSERT(mEvent.valid());
}

ScheduledWorker::~ScheduledWorker()
{
	NEW_LOG_BLOCK();

	this->OnSvcStop();

	traceW(L"close event");
	mEvent.close();
}

NTSTATUS ScheduledWorker::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM*)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);

	if (mEvent.invalid())
	{
		traceW(L"mEvent is null");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	const auto klassName{ getDerivedClassNamesW(this) };

#if !ENABLE_WORKER
	traceW(L"***");
	traceW(L"***     W A R N N I N G");
	traceW(L"***   %s disabled", klassName.c_str());
	traceW(L"***");
#endif

	for (int i=0; i<WORKER_MAX; i++)
	{
		EventHandle hStarted = ::CreateEventW(NULL, FALSE, FALSE, NULL);
		APP_ASSERT(hStarted.valid());

		auto& thr = mThreads.emplace_back(&ScheduledWorker::listen, this, i, hStarted.handle());
		::SwitchToThread();

		// スレッドが開始されたことを待つ

		if (::WaitForSingleObject(hStarted.handle(), INFINITE) != WAIT_OBJECT_0)
		{
			errorW(L"fault: WaitForSingleObject(%d)", i);
			return STATUS_UNSUCCESSFUL;
		}

		const auto priority = this->getThreadPriority();

		std::wostringstream ss;

		ss << klassName;
		ss << L" ThreaPriority=";
		ss << priority;
		ss << L" index=";
		ss << i;

		const auto ssStr{ ss.str() };

		auto h = thr.native_handle();
		const auto hresult = ::SetThreadDescription(h, ssStr.c_str());
		APP_ASSERT(SUCCEEDED(hresult));

		BOOL b = ::SetThreadPriority(h, priority);
		APP_ASSERT(b);

		traceW(L"worker [%s] started", ssStr.c_str());
	}

	return STATUS_SUCCESS;
}

VOID ScheduledWorker::OnSvcStop()
{
	NEW_LOG_BLOCK();

	// デストラクタからも呼び出されるので、再入可能としておくこと

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

	mTasks.clear();
}

void ScheduledWorker::listen(int argThreadIndex, HANDLE argStarted)
{
	NEW_LOG_BLOCK();

	if (!::SetEvent(argStarted))
	{
		errorW(L"fault: SetEvent(%d)", argThreadIndex);
		return;
	}

	const auto timePeriod = this->getTimePeriodMillis();
	const auto klassName{ getDerivedClassNamesW(this) };
	const auto klassNameCstr = klassName.c_str();

	for (int loopCount=0; ; loopCount++)
	{
		traceW(L"%s(%d): WaitForSingleObject ...", klassNameCstr, argThreadIndex);
		const auto reason = ::WaitForSingleObject(mEvent.handle(), timePeriod);	// 1 分間隔

		bool breakLoop = false;

		if (mEndWorkerFlag)
		{
			traceW(L"%s(%d): receive end worker request", klassNameCstr, argThreadIndex);

			breakLoop = true;
		}
		else
		{
			// reason の値が何であれ、!mEndWorkerFlag である間はタスクの処理を実行する

			switch (reason)
			{
				case WAIT_TIMEOUT:
				{
					// タイムアウトでの処理

					traceW(L"%s(%d): wait for signal: timeout occurred", klassNameCstr, argThreadIndex);

					break;
				}

				default:
				{
					// SetEvent の実行、又はシステムエラー
					// --> スケジュール起動なので SetEvent は発生しないはず

					const auto lerr = ::GetLastError();
					errorW(L"%s(%d): wait for signal: error reason=%lu lerr=%lu, break", klassNameCstr, argThreadIndex, reason, lerr);

					break;
				}
			}
		}

		if (breakLoop)
		{
			traceW(L"%s(%d): catch end-loop request, break", klassNameCstr, argThreadIndex);
			break;
		}

		// リストに入っているタスクを処理

		for (const auto& task: getTasks())
		{
			if (!task->shouldRun(loopCount))
			{
				continue;
			}

			try
			{
				traceW(L"%s(%d): run idle task ...", klassNameCstr, argThreadIndex);
				task->run(argThreadIndex);
				traceW(L"%s(%d): run idle task done", klassNameCstr, argThreadIndex);

				// 処理するごとに他のスレッドに回す
				::SwitchToThread();
			}
			catch (const std::exception& ex)
			{
				errorA("%s(%d): catch exception: what=[%s]", klassNameCstr, argThreadIndex, ex.what());
			}
			catch (...)
			{
				errorA("%s(%d): unknown error", klassNameCstr, argThreadIndex);
			}
		}
	}

	traceW(L"%s(%d): exit event loop", klassNameCstr, argThreadIndex);
}

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//

#if defined(THREAD_SAFE)
#error "THREAD_SAFFE(): already defined"
#endif

#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ mGuard }

bool ScheduledWorker::addTypedTask(IScheduledTask* argTask)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask);

#if ENABLE_WORKER
	mTasks.emplace_back(argTask);

	return true;

#else
	// ワーカー処理が無効な場合は、タスクのリクエストを無視

	argTask->cancelled();
	delete argTask;

	return false;
#endif
}

std::deque<std::shared_ptr<IScheduledTask>> ScheduledWorker::getTasks() const
{
	THREAD_SAFE();

	auto copy{ mTasks };

	return copy;
}

}	// namespace CSEDRV

// EOF