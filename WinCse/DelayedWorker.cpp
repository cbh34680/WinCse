#include "WinCseLib.h"
#include "DelayedWorker.hpp"
#include <filesystem>
#include <sstream>

using namespace WinCseLib;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// �^�X�N�������L��
const int WORKER_MAX = 2;

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

bool DelayedWorker::OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);

	if (mEvent.invalid())
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
		NTSTATUS ntstatus = ::SetThreadDescription(h, ss.str().c_str());
		//::SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);

		APP_ASSERT(NT_SUCCESS(ntstatus));
	}

	return true;
}

void DelayedWorker::OnSvcStop()
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

void DelayedWorker::listenEvent(const int argThreadIndex)
{
	NEW_LOG_BLOCK();

	while (1)
	{
		try
		{
			traceW(L"(%d): wait for signal ...", argThreadIndex);
			const auto reason = ::WaitForSingleObject(mEvent.handle(), INFINITE);

			if (mEndWorkerFlag)
			{
				traceW(L"(%d): receive end worker request", argThreadIndex);
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
					traceW(L"(%d): wait for signal: catch signal", argThreadIndex);
					break;
				}
				default:
				{
					traceW(L"(%d): wait for signal: error code=%ld, continue", argThreadIndex, reason);
					throw std::runtime_error("illegal route");

					break;
				}
			}

			// �L���[�ɓ����Ă���^�X�N������
			while (1)
			{
				auto task{ dequeueTask() };
				if (!task)
				{
					traceW(L"(%d): no more oneshot-tasks", argThreadIndex);
					break;
				}

				traceW(L"(%d): run oneshot task ...", argThreadIndex);
				task->run(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
				traceW(L"(%d): run oneshot task done", argThreadIndex);

				// �������邲�Ƃɑ��̃X���b�h�ɉ�
				//::SwitchToThread();
			}
		}
		catch (const std::exception& err)
		{
			traceA("(%d): what: %s", argThreadIndex, err.what());
			break;
		}
		catch (...)
		{
			traceA("(%d): unknown error, continue", argThreadIndex);
		}
	}


	// �c�����^�X�N�̓L�����Z�����Ĕj��

	while (1)
	{
		auto task{ dequeueTask() };
		if (!task)
		{
			traceW(L"(%d): no more oneshot-tasks", argThreadIndex);
			break;
		}

		task->cancelled(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
	}

	traceW(L"(%d): exit event loop", argThreadIndex);
}

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
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

	if (mEndWorkerFlag)
	{
		// �O�̂��߁A���[�J�[�E�X���b�h��~��̃��N�G�X�g�͔j��
		// --> ��{�I�ɂ͂��肦�Ȃ�

		argTask->cancelled(CONT_CALLER0);

		delete argTask;

		return false;
	}

	bool added = false;

#if ENABLE_TASK
	argTask->mPriority = priority;
	argTask->mAddTime = GetCurrentUtcMillis();
	argTask->mCaller = _wcsdup(CALL_CHAIN().c_str());

	if (priority == Priority::High)
	{
		// �D�悷��ꍇ�͐擪�ɒǉ�

		mTaskQueue.emplace_front(argTask);

		added = true;
	}
	else
	{
		// �ʏ�͌���ɒǉ�
		
		if (ignState == CanIgnoreDuplicates::Yes)
		{
			const auto argTaskName{ argTask->synonymString() };

			// �����\�̎��ɂ� synonym �͐ݒ肳���ׂ�
			APP_ASSERT(!argTaskName.empty());

			// �����\
			const auto it = std::find_if(mTaskQueue.begin(), mTaskQueue.end(), [&argTaskName](const auto& task)
			{
				// �L���[���瓯���V�m�j����T��
				return task->synonymString() == argTaskName;
			});

			if (it == mTaskQueue.end())
			{
				// �����̂��̂����݂��Ȃ�
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
			// �����ł��Ȃ�
			added = true;
		}

		if (added)
		{
			// ����ɒǉ�

			mTaskQueue.emplace_back(argTask);
		}
	}

	if (added)
	{
#if 1
		// Priority, AddTime �̏��Ƀ\�[�g

		std::sort(mTaskQueue.begin(), mTaskQueue.end(), taskComparator);
#endif

		// WaitForSingleObject() �ɒʒm
		const auto b = ::SetEvent(mEvent.handle());
		APP_ASSERT(b);
	}
	else
	{
		argTask->cancelled(CONT_CALLER0);

		delete argTask;
	}

#else
	// ���[�J�[�����������ȏꍇ�́A�^�X�N�̃��N�G�X�g�𖳎�
	argTask->cancelled();

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
	// ���[�J�[�����������ȏꍇ�́Anull ��ԋp

#endif

	return nullptr;
}

// EOF