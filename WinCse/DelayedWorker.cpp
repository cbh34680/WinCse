#include "DelayedWorker.hpp"

using namespace CSELIB;
using namespace CSEDRV;


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
		errorW(L"mEvent is null");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	const std::filesystem::path workDir{ argWorkDir };
	const auto confPath{ workDir / CONFIGFILE_FNAME };

	traceW(L"confPath=%s", confPath.c_str());

	int core_count = std::thread::hardware_concurrency();
	if (core_count > 8)
	{
		core_count = 8;
	}

	const auto numThreads = GetIniIntW(confPath, mIniSection, L"file_io_threads", core_count, 1, 32);

	traceW(L"numThreads=%d", numThreads);

	for (int i=0; i<numThreads; i++)
	{
		EventHandle hStarted = ::CreateEventW(NULL, FALSE, FALSE, NULL);
		APP_ASSERT(hStarted.valid());

		auto& thr = mThreads.emplace_back(&DelayedWorker::listen, this, i, hStarted.handle());
		::SwitchToThread();

		// スレッドが開始されたことを待つ

		if (::WaitForSingleObject(hStarted.handle(), INFINITE) != WAIT_OBJECT_0)
		{
			errorW(L"fault: WaitForSingleObject(%d)", i);
			return STATUS_UNSUCCESSFUL;
		}

		std::wostringstream ss;
		ss << L"WinCse::DelayedWorker ";
		ss << i;

		auto h = thr.native_handle();
		const auto hresult = ::SetThreadDescription(h, ss.str().c_str());
		APP_ASSERT(SUCCEEDED(hresult));
		
		BOOL b = ::SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
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

void DelayedWorker::listen(int argThreadIndex, HANDLE argStarted)
{
	NEW_LOG_BLOCK();

	if (!::SetEvent(argStarted))
	{
		errorW(L"fault: SetEvent(%d)", argThreadIndex);
		return;
	}

	while (true)
	{
		traceW(L"(%d): WaitForSingleObject ...", argThreadIndex);
		const auto reason = ::WaitForSingleObject(mEvent.handle(), INFINITE);

		bool breakLoop = false;

		if (mEndWorkerFlag)
		{
			traceW(L"(%d): receive end worker request", argThreadIndex);

			breakLoop = true;
		}
		else
		{
			// reason の値が何であれ、!mEndWorkerFlag である間はキューにある処理を実行する

			switch (reason)
			{
				case WAIT_OBJECT_0:
				{
					// SetEvent の実行

					traceW(L"(%d): wait for signal: catch signal", argThreadIndex);

					break;
				}

				default:
				{
					// タイムアウト、又はシステムエラー
					// --> INFINITE なのでタイムアウトは発生しないはず

					const auto lerr = ::GetLastError();
					errorW(L"(%d): wait for signal: error code=%lu lerr=%lu", argThreadIndex, reason, lerr);

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

		while (true)
		{
			auto task{ dequeueTask() };
			if (!task)
			{
				traceW(L"(%d): no more oneshot-tasks", argThreadIndex);
				break;
			}

			try
			{
				traceW(L"(%d): run oneshot task ...", argThreadIndex);
				task->run(argThreadIndex);
				traceW(L"(%d): run oneshot task done", argThreadIndex);
			}
			catch (const std::exception& ex)
			{
				errorA("(%d): what: %s, continue", argThreadIndex, ex.what());
			}
			catch (...)
			{
				errorA("(%d): unknown error, continue", argThreadIndex);
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

#if defined(THREAD_SAFE)
#error "THREAD_SAFFE(): already defined"
#endif

#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ mGuard }

bool DelayedWorker::addTypedTask(IOnDemandTask* argTask)
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

std::unique_ptr<IOnDemandTask> DelayedWorker::dequeueTask()
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

// EOF