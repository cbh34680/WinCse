#include "ScheduledWorker.hpp"
#include <numeric>

using namespace CSELIB;
using namespace CSEDRV;


#define ENABLE_WORKER		(1)

#if ENABLE_WORKER
static const int WORKER_MAX = 1;
#else
static const int WORKER_MAX = 0;
#endif

ScheduledWorker::ScheduledWorker(const std::wstring& argIniSection)
	:
	mIniSection(argIniSection)
{
	// OnSvcStart �̌Ăяo�����ɂ��C�x���g�I�u�W�F�N�g��������
	// ������邽�߁A�R���X�g���N�^�Ő������� OnSvcStart �� null �`�F�b�N����

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
		auto& thr = mThreads.emplace_back(&ScheduledWorker::listen, this, i);

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

	// �f�X�g���N�^������Ăяo�����̂ŁA�ē��\�Ƃ��Ă�������

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

void ScheduledWorker::listen(int argThreadIndex)
{
	NEW_LOG_BLOCK();

	const auto timePeriod = this->getTimePeriodMillis();
	const auto klassName{ getDerivedClassNamesW(this) };
	const auto klassNameCstr = klassName.c_str();

	for (int loopCount=0; ; loopCount++)
	{
		traceW(L"%s(%d): WaitForSingleObject ...", klassNameCstr, argThreadIndex);
		const auto reason = ::WaitForSingleObject(mEvent.handle(), timePeriod);	// 1 ���Ԋu

		bool breakLoop = false;

		if (mEndWorkerFlag)
		{
			traceW(L"%s(%d): receive end worker request", klassNameCstr, argThreadIndex);

			breakLoop = true;
		}
		else
		{
			// reason �̒l�����ł���A!mEndWorkerFlag �ł���Ԃ̓^�X�N�̏��������s����

			switch (reason)
			{
				case WAIT_TIMEOUT:
				{
					// �^�C���A�E�g�ł̏���

					traceW(L"%s(%d): wait for signal: timeout occurred", klassNameCstr, argThreadIndex);

					break;
				}

				default:
				{
					// SetEvent �̎��s�A���̓V�X�e���G���[
					// --> �X�P�W���[���N���Ȃ̂� SetEvent �͔������Ȃ��͂�

					const auto lerr = ::GetLastError();
					traceW(L"%s(%d): wait for signal: error reason=%lu lerr=%lu, break", klassNameCstr, argThreadIndex, reason, lerr);

					break;
				}
			}
		}

		if (breakLoop)
		{
			traceW(L"%s(%d): catch end-loop request, break", klassNameCstr, argThreadIndex);
			break;
		}

		// ���X�g�ɓ����Ă���^�X�N������

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

				// �������邲�Ƃɑ��̃X���b�h�ɉ�
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
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//

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
	// ���[�J�[�����������ȏꍇ�́A�^�X�N�̃��N�G�X�g�𖳎�

	argTask->cancelled();
	delete argTask;

	return false;
#endif
}

std::deque<std::shared_ptr<IScheduledTask>> ScheduledWorker::getTasks() const
{
	THREAD_SAFE();

	return mTasks;
}

// EOF