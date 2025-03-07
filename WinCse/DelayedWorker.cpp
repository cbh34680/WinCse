#include "WinCseLib.h"
#include "DelayedWorker.hpp"
#include <filesystem>
#include <queue>
#include <mutex>
#include <sstream>

using namespace WinCseLib;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// �^�X�N�������L��
const int WORKER_MAX = 4;

#else
// �^�X�N����������
const int WORKER_MAX = 0;

#endif

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
		mThreads.emplace_back(&DelayedWorker::listenEvent, this, i);
	}

	return true;
}

struct BreakLoopRequest : public std::exception
{
	BreakLoopRequest(char const* const msg) : std::exception(msg) { }
};

struct BreakLoopTask : public ITask
{
	void run(CALLER_ARG0) override
	{
		NEW_LOG_BLOCK();

		traceW(L"throw break");

		throw BreakLoopRequest("from " __FUNCTION__);
	}
};

void DelayedWorker::OnSvcStop()
{
	NEW_LOG_BLOCK();

	// �f�X�g���N�^������Ă΂��̂ŁA�ē��\�Ƃ��Ă�������

	if (!mThreads.empty())
	{
		traceW(L"wait for thread end ...");

		for (int i=0; i<mThreads.size(); i++)
		{
			// �ŗD��̒�~����
			addTask(START_CALLER new BreakLoopTask, Priority::High, CanIgnore::No);
		}

		for (auto& thr: mThreads)
		{
			thr.join();
		}

		mThreads.clear();

		traceW(L"done.");
	}
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

			// �L���[�ɓ����Ă���^�X�N������
			while (1)
			{
				auto task{ dequeueTask() };
				if (!task)
				{
					traceW(L"(%d): no more oneshot-tasks", i);
					break;
				}

				traceW(L"(%d): run oneshot task ...", i);
				task->mWorkerId_4debug = i;
				task->run(task->mCaller_4debug + L"->" + __FUNCTIONW__);
				traceW(L"(%d): run oneshot task done", i);

				// �������邲�Ƃɑ��̃X���b�h�ɉ�
				//::SwitchToThread();
			}
		}
		catch (const BreakLoopRequest&)
		{
			traceA("(%d): catch loop-break request, go exit thread", i);
			break;
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
// �����o�ɔz�u����Ƃ��̃t�@�C���ȊO������A�N�Z�X�ł��Ă��܂�
// ���Ƃ̉�����u�B
// 
// static �ɒu���ƃ��������[�N�Ƃ��ĔF�����ꂽ��A���낢��ʓ|�Ȃ̂�
// �R���X�g���N�^�ƃf�X�g���N�^�ŊǗ�����
//
static struct LocalData
{
	std::mutex mGuard;
	std::deque<std::unique_ptr<WinCseLib::ITask>> mTaskQueue;
}
*Local;

DelayedWorker::DelayedWorker(const std::wstring& tmpdir, const std::wstring& iniSection)
	: mTempDir(tmpdir), mIniSection(iniSection), mTaskSkipCount(0)
{
	Local = new LocalData;
	APP_ASSERT(Local);

	// OnSvcStart �̌Ăяo�����ɂ��C�x���g�I�u�W�F�N�g��������
	// ������邽�߁A�R���X�g���N�^�Ő������� OnSvcStart �� null �`�F�b�N����

	mEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	APP_ASSERT(mEvent);
}

DelayedWorker::~DelayedWorker()
{
	NEW_LOG_BLOCK();

	this->OnSvcStop();

	traceW(L"close event");
	::CloseHandle(mEvent);

	delete Local;
}


//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(Local->mGuard)


bool DelayedWorker::addTask(CALLER_ARG WinCseLib::ITask* argTask, WinCseLib::Priority priority, WinCseLib::CanIgnore ignState)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask)

	bool added = false;

#if ENABLE_TASK
	argTask->mPriority = priority;
	argTask->mCaller_4debug = CALL_CHAIN();

	if (priority == Priority::High)
	{
		// �D�悷��ꍇ�͐擪�ɒǉ�

		Local->mTaskQueue.emplace_front(argTask);

		added = true;
	}
	else
	{
		// �ʏ�͌���ɒǉ�
		
		if (ignState == CanIgnore::Yes)
		{
			const auto argTaskName{ argTask->synonymString() };

			// �L�����Z���\�̎��ɂ� synonym �͐ݒ肳���ׂ�
			APP_ASSERT(!argTaskName.empty());

			// �����\
			const auto it = std::find_if(Local->mTaskQueue.begin(), Local->mTaskQueue.end(), [&argTaskName](const auto& task)
			{
				// �L���[���瓯���V�m�j����T��
				return task->synonymString() == argTaskName;
			});

			if (it == Local->mTaskQueue.end())
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

			Local->mTaskQueue.emplace_back(argTask);
		}
	}

	if (added)
	{
		// WaitForSingleObject() �ɒʒm
		const auto b = ::SetEvent(mEvent);
		APP_ASSERT(b);
	}
	else
	{
		delete argTask;
	}

#else
	// ���[�J�[�����������ȏꍇ�́A�^�X�N�̃��N�G�X�g�𖳎�
	delete argTask;

#endif

	return added;
}

std::unique_ptr<ITask> DelayedWorker::dequeueTask()
{
	THREAD_SAFE();
	//NEW_LOG_BLOCK();

#if ENABLE_TASK
	if (!Local->mTaskQueue.empty())
	{
		auto ret{ std::move(Local->mTaskQueue.front()) };
		Local->mTaskQueue.pop_front();

		return ret;
	}

#else
	// ���[�J�[�����������ȏꍇ�́Anull ��ԋp

#endif

	return nullptr;
}

// EOF