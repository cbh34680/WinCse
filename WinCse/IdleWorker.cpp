#include "WinCseLib.h"
#include "IdleWorker.hpp"
#include <filesystem>
#include <mutex>
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
	// OnSvcStart �̌Ăяo�����ɂ��C�x���g�I�u�W�F�N�g��������
	// ������邽�߁A�R���X�g���N�^�Ő������� OnSvcStart �� null �`�F�b�N����

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

	// �f�X�g���N�^������Ăяo�����̂ŁA�ē��\�Ƃ��Ă�������

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
	// ���O�̋L�^�񐔂͏󋵂ɂ���ĕω����邽�߁A�J�n�����萔��
	// �L�^�񐔂��̎悵�A��������Z�o������l��A�����ĉ�������ꍇ��
	// �A�C�h�����̃^�X�N�����s����B
	// 
	// �A���A���̃��O�L�^�������ԑ������ꍇ�Ƀ^�X�N�����s�ł��Ȃ��Ȃ�
	// ���Ƃ��l�����āA���Ԗʂł̃^�X�N���s�����{����B
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
			// �ő� 10 �b�ԑҋ@
			// ���̐��l��ς���Ƃ��̓��O�L�^�񐔂ɂ����ӂ���

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
					// ������s
					traceW(L"!!! *** DANGER *** !!! force IdleTime on each timeout for DEBUG");

					idleCount = IDLE_TASK_EXECUTE_THRESHOLD + 1;

#else
					traceW(L"thisCount=%d", thisCount);

					if (logCountHist9.size() < 9)
					{
						// ���Z�b�g���� 9 ��(10s * 9 = 1m30s) �̓��O�L�^�񐔂����W

						traceW(L"collect log count, %zu", logCountHist9.size());
						idleCount = 0;
					}
					else
					{
						// �ߋ� 9 ��̃��O�L�^�񐔂����l���Z�o

						const int sumHist9 = (int)std::accumulate(logCountHist9.begin(), logCountHist9.end(), 0);
						const int avgHist9 = sumHist9 / (int)logCountHist9.size();

						const int refHist9 = avgHist9 / 4; // 25%

						traceW(L"sumHist9=%d, avgHist9=%d, refHist9=%d", sumHist9, avgHist9, refHist9);

						if (thisCount < refHist9)
						{
							// ����̋L�^����l��艺�Ȃ�A�C�h�����ԂƂ��ăJ�E���g

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

					// �V�O�i��������(�X���b�h�̏I���v��) �͑����Ɏ��s�ł���悤�ɃJ�E���g�𒲐�

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
				// 10 ���ȏ���s����Ă��Ȃ��ꍇ�̋~�ϑ[�u

				idleCount = IDLE_TASK_EXECUTE_THRESHOLD + 1;

				traceW(L"force execute idle-task");
			}

			traceW(L"idleCount: %d", idleCount);

			if (idleCount >= IDLE_TASK_EXECUTE_THRESHOLD)
			{
				// �A�C�h�����Ԃ���萔�A�������ꍇ�A�������͗D��x�������ꍇ�Ƀ^�X�N�����s

				traceW(L"exceeded the threshold.");

				// �L���[�ɓ����Ă���^�X�N������
				const auto tasks{ getTasks() };

				for (const auto& task: tasks)
				{
					if (task->mPriority != Priority::Low)
					{
						// �ً}�x�͒Ⴂ�̂ŁA���̃X���b�h��D�悳����

						const auto b = ::SwitchToThread();
						traceW(L"(%d): SwitchToThread return %s", b ? L"true" : L"false");
					}

					traceW(L"(%d): run idle task ...", i);
					task->run(std::wstring(task->mCaller) + L"->" + __FUNCTIONW__);
					traceW(L"(%d): run idle task done", i);

				}

				// �J�E���^�̏�����
				idleCount = 0;

				// �ŏI���s���Ԃ̍X�V
				lastExecTime = chrono::system_clock::now();

				// �L�^�̃��Z�b�g
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
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool IdleWorker::addTask(CALLER_ARG WinCseLib::ITask* argTask, WinCseLib::Priority priority, WinCseLib::CanIgnoreDuplicates ignState)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(argTask);

	// �����ۂ͈Ӗ����Ȃ��̂Őݒ肳���Ȃ�
	APP_ASSERT(ignState == CanIgnoreDuplicates::None);

#if ENABLE_WORKER
	argTask->mPriority = priority;
	argTask->mCaller = wcsdup(CALL_CHAIN().c_str());

	if (priority == Priority::High)
	{
		// �D�悷��ꍇ
		//traceW(L"add highPriority=true");
		Local->mTasks.emplace_front(argTask);

		// WaitForSingleObject() �ɒʒm
		const auto b = ::SetEvent(mEvent);
		APP_ASSERT(b);
	}
	else
	{
		// �ʏ�͂�����
		//traceW(L"add highPriority=false");
		Local->mTasks.emplace_back(argTask);
	}

	return true;

#else
	// ���[�J�[�����������ȏꍇ�́A�^�X�N�̃��N�G�X�g�𖳎�
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