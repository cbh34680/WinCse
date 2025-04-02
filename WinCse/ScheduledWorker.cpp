#include "WinCseLib.h"
#include "ScheduledWorker.hpp"
#include <filesystem>
#include <numeric>
#include <sstream>

using namespace WinCseLib;


#define ENABLE_WORKER		(1)

#if ENABLE_WORKER
static const int WORKER_MAX = 1;
#else
static const int WORKER_MAX = 0;
#endif

ScheduledWorker::ScheduledWorker(const std::wstring& argTempDir, const std::wstring& argIniSection)
	: mTempDir(argTempDir), mIniSection(argIniSection)
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

bool ScheduledWorker::OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);

	if (mEvent.invalid())
	{
		traceW(L"mEvent is null");
		return false;
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
		auto& thr = mThreads.emplace_back(&ScheduledWorker::listenEvent, this, i);

		const auto priority = this->getThreadPriority();

		std::wstringstream ss;
		ss << klassName;
		ss << L" priority=";
		ss << priority;
		ss << L" index=";
		ss << i;

		const auto ssStr{ ss.str() };

		auto h = thr.native_handle();
		NTSTATUS ntstatus = ::SetThreadDescription(h, ssStr.c_str());
		APP_ASSERT(NT_SUCCESS(ntstatus));

		BOOL b = ::SetThreadPriority(h, priority);
		APP_ASSERT(b);

		traceW(L"worker [%s] started", ssStr.c_str());
	}

	return true;
}

void ScheduledWorker::OnSvcStop()
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

void ScheduledWorker::listenEvent(const int threadIndex)
{
	NEW_LOG_BLOCK();

	const auto timePeriod = this->getTimePeriodMillis();
	const auto klassName{ getDerivedClassNamesW(this) };
	const auto klassNameCstr = klassName.c_str();

	for (int loopCount=0; ; loopCount++)
	{
		//traceW(L"%s(%d): wait for signal ...", klassNameCstr, threadIndex);
		const auto reason = ::WaitForSingleObject(mEvent.handle(), timePeriod);	// 1 分間隔

		bool breakLoop = false;

		if (mEndWorkerFlag)
		{
			traceW(L"%s(%d): receive end worker request", klassNameCstr, threadIndex);

			breakLoop = true;
		}
		else
		{
			switch (reason)
			{
				case WAIT_TIMEOUT:
				{
					// タイムアウトでの処理

					//traceW(L"%s(%d): wait for signal: timeout occurred", klassNameCstr, threadIndex);
					break;
				}

				default:
				{
					// SetEvent の実行、又はシステムエラー

					const auto lerr = ::GetLastError();
					traceW(L"%s(%d): wait for signal: error reason=%ld error=%ld, break",
						klassNameCstr, threadIndex, reason, lerr);

					breakLoop = true;

					break;
				}
			}
		}

		if (breakLoop)
		{
			traceW(L"%s(%d): catch end-loop request, break", klassNameCstr, threadIndex);
			break;
		}

		// リストに入っているタスクを処理

		for (const auto& task: getTasks())
		{
			if (task->shouldRun(loopCount))
			{
				try
				{
					//traceW(L"%s(%d): run idle task ...", klassNameCstr, threadIndex);
					task->run(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
					//traceW(L"%s(%d): run idle task done", klassNameCstr, threadIndex);
				}
				catch (const std::exception& ex)
				{
					traceA("%s(%d): catch exception: what=[%s]", klassNameCstr, threadIndex, ex.what());
				}
				catch (...)
				{
					traceA("%s(%d): unknown error", klassNameCstr, threadIndex);
				}
			}
		}
	}

	traceW(L"%s(%d): exit event loop", klassNameCstr, threadIndex);
}

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool ScheduledWorker::addTypedTask(CALLER_ARG WinCseLib::IScheduledTask* argTask)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask);

#if ENABLE_WORKER
	argTask->mCaller = _wcsdup(CALL_CHAIN().c_str());
	mTasks.emplace_back(argTask);

	return true;

#else
	// ワーカー処理が無効な場合は、タスクのリクエストを無視

	argTask->cancelled(CONT_CALLER0);
	delete argTask;

	return false;
#endif
}

std::deque<std::shared_ptr<IScheduledTask>> ScheduledWorker::getTasks()
{
	THREAD_SAFE();

	return mTasks;
}

// EOF