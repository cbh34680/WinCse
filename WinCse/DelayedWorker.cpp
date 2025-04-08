#include "WinCseLib.h"
#include "DelayedWorker.hpp"
#include <filesystem>
#include <sstream>

using namespace WCSE;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// �^�X�N�������L��
const int WORKER_MAX = 4;

#else
// �^�X�N����������
const int WORKER_MAX = 0;

#endif

DelayedWorker::DelayedWorker(const std::wstring& tmpdir, const std::wstring& iniSection)
	: mTempDir(tmpdir), mIniSection(iniSection), mTaskSkipCount(0)
{
	// OnSvcStart �̌Ăяo�����ɂ��C�x���g�I�u�W�F�N�g��������
	// ������邽�߁A�R���X�g���N�^�Ő������� OnSvcStart �� null �`�F�b�N����

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

NTSTATUS DelayedWorker::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
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

		std::wstringstream ss;
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

	// �f�X�g���N�^������Ă΂��̂ŁA�ē��\�Ƃ��Ă�������

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

void DelayedWorker::listenEvent(const int threadIndex)
{
	NEW_LOG_BLOCK();

	while (1)
	{
		//traceW(L"(%d): wait for signal ...", threadIndex);
		const auto reason = ::WaitForSingleObject(mEvent.handle(), INFINITE);

		bool breakLoop = false;

		if (mEndWorkerFlag)
		{
			traceW(L"(%d): receive end worker request", threadIndex);

			breakLoop = true;
		}
		else
		{
			switch (reason)
			{
				case WAIT_OBJECT_0:
				{
					// SetEvent �̎��s

					//traceW(L"(%d): wait for signal: catch signal", threadIndex);
					break;
				}

				default:
				{
					// �^�C���A�E�g�A���̓V�X�e���G���[

					traceW(L"(%d): wait for signal: error code=%ld, break", threadIndex, reason);

					breakLoop = true;

					break;
				}
			}
		}

		if (breakLoop)
		{
			traceW(L"(%d): catch end-loop request, break", threadIndex);
			break;
		}

		// �L���[�ɓ����Ă���^�X�N������

		while (1)
		{
			auto task{ dequeueTask() };
			if (!task)
			{
				//traceW(L"(%d): no more oneshot-tasks", threadIndex);
				break;
			}

			try
			{
				//traceW(L"(%d): run oneshot task ...", threadIndex);
				task->run(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
				//traceW(L"(%d): run oneshot task done", threadIndex);
			}
			catch (const std::exception& ex)
			{
				traceA("(%d): what: %s", threadIndex, ex.what());
				break;
			}
			catch (...)
			{
				traceA("(%d): unknown error, continue", threadIndex);
			}
		}
	}

	// �c�����^�X�N�̓L�����Z�����Ĕj��

	while (1)
	{
		auto task{ dequeueTask() };
		if (!task)
		{
			traceW(L"(%d): no more oneshot-tasks", threadIndex);
			break;
		}

		const auto klassName{ getDerivedClassNamesW(task.get()) };

		traceW(L"cancel task=%s", klassName.c_str());
		task->cancelled(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
	}

	traceW(L"(%d): exit event loop", threadIndex);
}

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
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
		// �O�̂��߁A���[�J�[�E�X���b�h��~��̃��N�G�X�g�͔j��
		// --> ��{�I�ɂ͂��肦�Ȃ�

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

		// �����\�̎��ɂ� synonym �͐ݒ肳���ׂ�

		APP_ASSERT(!taskName.empty());

		// �L���[���瓯���V�m�j�������^�X�N��T��

		const auto it = std::find_if(mTaskQueue.begin(), mTaskQueue.end(), [&taskName](const auto& task)
		{
			return task->synonymString() == taskName;
		});

		if (it == mTaskQueue.end())
		{
			// �����̂��̂����݂��Ȃ�

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
		// �����ł��Ȃ�

		add = true;
	}

	if (add)
	{
		mTaskQueue.emplace_back(argTask);
	}

	if (add)
	{
		// Priority, AddTime �̏��Ƀ\�[�g

		std::sort(mTaskQueue.begin(), mTaskQueue.end(), taskComparator);

		// WaitForSingleObject() �ɒʒm

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

#else

bool DelayedWorker::addTypedTask(CALLER_ARG WCSE::IOnDemandTask* argTask)
{
	THREAD_SAFE();

	// ���[�J�[�����������ȏꍇ�́A�^�X�N�̃��N�G�X�g�𖳎�

	argTask->cancelled(CONT_CALLER0);
	delete argTask;

	return add;
}

std::unique_ptr<IOnDemandTask> DelayedWorker::dequeueTask()
{
	THREAD_SAFE();

	// ���[�J�[�����������ȏꍇ�́Anull ��ԋp

	return nullptr;
}
#endif

// EOF