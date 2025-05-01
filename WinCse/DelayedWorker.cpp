#include "DelayedWorker.hpp"

using namespace CSELIB;
using namespace CSEDRV;


#define ENABLE_TASK		(1)


#if ENABLE_TASK
// �^�X�N�������L��
const int WORKER_MAX = 6;

#else
// �^�X�N����������
const int WORKER_MAX = 0;

#endif

DelayedWorker::DelayedWorker(const std::wstring& argIniSection)
	:
	mIniSection(argIniSection)
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
					// SetEvent �̎��s

					//traceW(L"(%d): wait for signal: catch signal", argThreadIndex);
					break;
				}

				default:
				{
					// �^�C���A�E�g�A���̓V�X�e���G���[

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

		// �L���[�ɓ����Ă���^�X�N������

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

	// �c�����^�X�N�̓L�����Z�����Ĕj��

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
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
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
		// �O�̂��߁A���[�J�[�E�X���b�h��~��̃��N�G�X�g�͔j��
		// --> ��{�I�ɂ͂��肦�Ȃ�

		argTask->cancelled();
		delete argTask;

		return false;
	}

	mTaskQueue.emplace_back(argTask);

	// WaitForSingleObject() �ɒʒm

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

	// ���[�J�[�����������ȏꍇ�́A�^�X�N�̃��N�G�X�g�𖳎�

	argTask->cancelled(CONT_CALLER0);
	delete argTask;

	return add;
}

std::unique_ptr<IOnDemandTask> DelayedWorker::dequeueTask() noexcept
{
	THREAD_SAFE();

	// ���[�J�[�����������ȏꍇ�́Anull ��ԋp

	return nullptr;
}
#endif

// EOF