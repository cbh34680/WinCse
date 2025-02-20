#include "WinCseLib.h"
#include "IdleWorker.hpp"
#include <filesystem>
#include <mutex>
#include <numeric>

#define ENABLE_TASK		(1)

const int WORKER_MAX = 1;


IdleWorker::IdleWorker(const wchar_t* tmpdir, const wchar_t* iniSection)
	: mTempDir(tmpdir), mIniSection(iniSection)
{
	// OnSvcStart の呼び出し順によるイベントオブジェクト未生成を
	// 回避するため、コンストラクタで生成して OnSvcStart で null チェックする

	mEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
}

IdleWorker::~IdleWorker()
{
	NEW_LOG_BLOCK();

	if (mEvent)
	{
		traceW(L"close event");
		::CloseHandle(mEvent);
	}
}

bool IdleWorker::OnSvcStart(const wchar_t* argWorkDir)
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
		mThreads.emplace_back(&IdleWorker::listenEvent, this, i);
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

void IdleWorker::OnSvcStop()
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
			const chrono::steady_clock::time_point start = chrono::steady_clock::now();

			traceW(L"(%d): wait for signal ...", i);
			const auto reason = ::WaitForSingleObject(mEvent, 1000 * 10);

			switch (reason)
			{
				case WAIT_TIMEOUT:
				{
					const int currCount = LogBlock::getCount();
					const int thisCount = currCount - prevCount;
					prevCount = currCount;

#if 0
					// 毎回実行
					idleCount = IDLE_TASK_EXECUTE_THRESHOLD + 1;

#else
					traceW(L"thisCount=%d", thisCount);

					if (logCountHist9.size() < 9)
					{
						// リセットから 9 回はログ記録回数を収集

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

						if (thisCount <= refHist9)
						{
							// 今回の記録が基準値以下ならアイドル時間としてカウント

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

					// シグナル到着時は即時に実行できるようにカウントを調整

					idleCount = IDLE_TASK_EXECUTE_THRESHOLD + 1;

					break;
				}

				default:
				{
					traceW(L"(%d): wait for signal: error code=%ld, continue", i, reason);
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
					if (!task->mPriority == Priority::LOW)
					{
						// 緊急度は低いので、他のスレッドを優先させる

						::SwitchToThread();
					}

					traceW(L"(%d): run idle task ...", i);
					task->_mWorkerId_4debug = i;
					task->run(INIT_CALLER this, LOG_DEPTH());
					traceW(L"(%d): run idle task done", i);

				}

				// カウンタの初期化
				idleCount = 0;

				// 最終実行時間の更新
				lastExecTime = chrono::system_clock::now();

				// 記録のリセット
				logCountHist9.clear();
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


bool IdleWorker::addTask(ITask* task, CanIgnore ignState, Priority priority)
{
	THREAD_SAFE();
	//NEW_LOG_BLOCK();
	APP_ASSERT(task);

#if ENABLE_TASK
	task->mPriority = priority;

	if (priority == Priority::HIGH)
	{
		// 優先する場合
		//traceW(L"add highPriority=true");
		mTasks.emplace_front(task);

		// WaitForSingleObject() に通知
		::SetEvent(mEvent);
	}
	else
	{
		// 通常はこちら
		//traceW(L"add highPriority=false");
		mTasks.emplace_back(task);
	}

	return true;

#else
	// ワーカー処理が無効な場合は、タスクのリクエストを無視
	delete task;

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