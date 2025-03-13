#include "WinCseLib.h"
#include "IdleWorker.hpp"
#include <filesystem>
#include <numeric>
#include <sstream>

using namespace WinCseLib;


#define ENABLE_WORKER		(0)

#if ENABLE_WORKER
static const int WORKER_MAX = 1;
#else
static const int WORKER_MAX = 0;
#endif

IdleWorker::IdleWorker(const std::wstring& argTempDir, const std::wstring& argIniSection)
	: mTempDir(argTempDir), mIniSection(argIniSection)
{
	// OnSvcStart の呼び出し順によるイベントオブジェクト未生成を
	// 回避するため、コンストラクタで生成して OnSvcStart で null チェックする

	mEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	APP_ASSERT(mEvent);
}

IdleWorker::~IdleWorker()
{
	NEW_LOG_BLOCK();

	this->OnSvcStop();

	traceW(L"close event");
	::CloseHandle(mEvent);
}

bool IdleWorker::OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);

	if (!mEvent)
	{
		traceW(L"mEvent is null");
		return false;
	}

#if !ENABLE_WORKER
	traceW(L"***                         ***");
	traceW(L"***     W A R N N I N G     ***");
	traceW(L"***   Idle Worker disabled  ***");
	traceW(L"***                         ***");
#endif

	for (int i=0; i<WORKER_MAX; i++)
	{
		auto& thr = mThreads.emplace_back(&IdleWorker::listenEvent, this, i);

		std::wstringstream ss;
		ss << L"WinCse::DelayedWorker ";
		ss << i;

		auto h = thr.native_handle();
		::SetThreadDescription(h, ss.str().c_str());
		//::SetThreadPriority(h, THREAD_PRIORITY_LOWEST);
	}

	return true;
}

void IdleWorker::OnSvcStop()
{
	NEW_LOG_BLOCK();

	// デストラクタからも呼び出されるので、再入可能としておくこと

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

	mTasks.clear();
}

void IdleWorker::listenEvent(const int i)
{
	NEW_LOG_BLOCK();

	namespace chrono = std::chrono;

	//
	// ログの記録回数は状況によって変化するため、開始から一定数の
	// 記録回数を採取し、そこから算出した基準値を連続して下回った場合は
	// アイドル時のタスクを実行する。
	// 
	// 但し、一定のログ記録が長時間続いた場合にタスクが実行できなくなる
	// ことを考慮して、時間面でのタスク実行も実施する。
	//
	std::deque<int> logCountHist9;

	const int IDLE_TASK_EXECUTE_THRESHOLD = 3;
	int idleCount = 0;

	auto lastExecTime{ chrono::system_clock::now() };

	int prevCount = LogBlock::getCount();

	while (1)
	{
		try
		{
			// 最大 10 秒間待機
			// この数値を変えるときはログ記録回数にも注意する

			traceW(L"(%d): wait for signal ...", i);
			const auto reason = ::WaitForSingleObject(mEvent, 1000 * 10);

			if (mEndWorkerFlag)
			{
				traceW(L"(%d): receive end worker request", i);
				break;
			}

			switch (reason)
			{
				case WAIT_TIMEOUT:
				{
					const int currCount = LogBlock::getCount();
					const int thisCount = currCount - prevCount;
					prevCount = currCount;

#if 0
					// 毎回実行
					traceW(L"!!! *** DANGER *** !!! force IdleTime on each timeout for DEBUG");

					idleCount = IDLE_TASK_EXECUTE_THRESHOLD + 1;

#else
					traceW(L"thisCount=%d", thisCount);

					if (logCountHist9.size() < 9)
					{
						// リセットから 9 回(10s * 9 = 1m30s) はログ記録回数を収集

						traceW(L"collect log count, %zu", logCountHist9.size());
						idleCount = 0;
					}
					else
					{
						// 過去 9 回のログ記録回数から基準値を算出

						const int sumHist9 = (int)std::accumulate(logCountHist9.begin(), logCountHist9.end(), 0);
						const int avgHist9 = sumHist9 / (int)logCountHist9.size();

						const int refHist9 = avgHist9 / 4; // 25%

						traceW(L"sumHist9=%d, avgHist9=%d, refHist9=%d", sumHist9, avgHist9, refHist9);

						if (thisCount < refHist9)
						{
							// 今回の記録が基準値より下ならアイドル時間としてカウント

							idleCount++;
						}
						else
						{
							idleCount = 0;
						}

						logCountHist9.pop_front();
					}

					logCountHist9.push_back(thisCount);
#endif

					break;
				}
				case WAIT_OBJECT_0:
				{
					traceW(L"(%d): wait for signal: catch signal", i);

					// シグナル到着時(スレッドの終了要請) は即時に実行できるようにカウントを調整

					idleCount = IDLE_TASK_EXECUTE_THRESHOLD + 1;

					break;
				}
				default:
				{
					const auto lerr = ::GetLastError();
					traceW(L"(%d): wait for signal: error reason=%ld error=%ld, continue", i, reason, lerr);
					
					throw std::runtime_error("illegal route");

					break;
				}
			}

			if (lastExecTime < (chrono::system_clock::now() - chrono::minutes(10)))
			{
				// 10 分以上実行されていない場合の救済措置

				idleCount = IDLE_TASK_EXECUTE_THRESHOLD + 1;

				traceW(L"force execute idle-task");
			}

			traceW(L"idleCount: %d", idleCount);

			if (idleCount >= IDLE_TASK_EXECUTE_THRESHOLD)
			{
				// アイドル時間が一定数連続した場合、もしくは優先度が高い場合にタスクを実行

				traceW(L"exceeded the threshold.");

				// キューに入っているタスクを処理
				const auto tasks{ getTasks() };

				for (const auto& task: tasks)
				{
					if (task->mPriority != Priority::Low)
					{
						// 緊急度は低いので、他のスレッドを優先させる

						const auto b = ::SwitchToThread();
						traceW(L"(%d): SwitchToThread return %s", b ? L"true" : L"false");
					}

					traceW(L"(%d): run idle task ...", i);
					task->run(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
					traceW(L"(%d): run idle task done", i);

				}

				// カウンタの初期化
				idleCount = 0;

				// 最終実行時間の更新
				lastExecTime = chrono::system_clock::now();

				// 記録のリセット
				//logCountHist9.clear();
			}
		}
		catch (const std::exception& ex)
		{
			traceA("(%d): catch exception: what=[%s], abort", i, ex.what());
			break;
		}
		catch (...)
		{
			traceA("(%d): unknown error, abort", i);
			break;
		}
	}

	traceW(L"(%d): exit event loop", i);
}

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool IdleWorker::addTask(CALLER_ARG WinCseLib::ITask* argTask, WinCseLib::Priority priority, WinCseLib::CanIgnoreDuplicates ignState)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask);

	// 無視可否は意味がないので設定させない
	APP_ASSERT(ignState == CanIgnoreDuplicates::None);

#if ENABLE_WORKER
	argTask->mPriority = priority;
	argTask->mCaller = wcsdup(CALL_CHAIN().c_str());

	if (priority == Priority::High)
	{
		// 優先する場合
		//traceW(L"add highPriority=true");
		Local->mTasks.emplace_front(argTask);

		// WaitForSingleObject() に通知
		const auto b = ::SetEvent(mEvent);
		APP_ASSERT(b);
	}
	else
	{
		// 通常はこちら
		//traceW(L"add highPriority=false");
		Local->mTasks.emplace_back(argTask);
	}

	return true;

#else
	// ワーカー処理が無効な場合は、タスクのリクエストを無視
	delete argTask;

	return false;
#endif
}

std::deque<std::shared_ptr<ITask>> IdleWorker::getTasks()
{
	THREAD_SAFE();
	//NEW_LOG_BLOCK();

	return mTasks;
}

// EOF