#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>


using namespace WinCseLib;


WinCse::WinCse(WINCSE_DRIVER_STATS* argStats,
	const std::wstring& argTempDir, const std::wstring& argIniSection,
	NamedWorker argWorkers[], ICSDevice* argCSDevice)
	:
	mStats(argStats),
	mTempDir(argTempDir), mIniSection(argIniSection),
	mCSDevice(argCSDevice),
	mMaxFileSize(-1),
	mResourceSweeper(this),
	mIgnoredFileNamePatterns{ LR"(\b(desktop\.ini|autorun\.inf|(eh)?thumbs\.db|AlbumArtSmall\.jpg|folder\.(ico|jpg|gif)|\.DS_Store)$)", std::regex_constants::icase }
{
	NEW_LOG_BLOCK();

	APP_ASSERT(std::filesystem::exists(argTempDir));
	APP_ASSERT(std::filesystem::is_directory(argTempDir));
	APP_ASSERT(argCSDevice);

	NamedWorkersToMap(argWorkers, &mWorkers);
}

WinCse::~WinCse()
{
	NEW_LOG_BLOCK();

	traceW(L"close handle");

	mRefFile.close();
	mRefDir.close();

	traceW(L"all done.");
}

//
// �G�N�X�v���[���[���J�����܂ܐؒf����� WinFsp �� Close �����s����Ȃ� (�ׂ��Ǝv��)
// �̂ŁADoOpen ���Ă΂�� DoClose ���Ă΂�Ă��Ȃ����̂́A�A�v���P�[�V�����I������
// �����I�� DoClose ���Ăяo��
// 
// ���u���Ă����͂Ȃ����A�f�o�b�O���Ƀ��������[�N�Ƃ��ĕ񍐂���Ă��܂�
// �{���̈Ӗ��ł̃��������[�N�ƍ��݂��Ă��܂�����
//
WinCse::ResourceSweeper::~ResourceSweeper()
{
	NEW_LOG_BLOCK();

	// DoClose �� mOpenAddrs.erase() ������̂ŃR�s�[���K�v

	auto copy{ mOpenAddrs };

	for (auto& FileContext: copy)
	{
		::InterlockedIncrement(&(mThat->mStats->_ForceClose));

		traceW(L"force close address=%p", FileContext);

		mThat->DoClose(FileContext);
	}
}

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

void WinCse::ResourceSweeper::add(PTFS_FILE_CONTEXT* FileContext)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(mOpenAddrs.find(FileContext) == mOpenAddrs.end());

	traceW(L"add address=%p", FileContext);

	mOpenAddrs.insert(FileContext);
}

void WinCse::ResourceSweeper::remove(PTFS_FILE_CONTEXT* FileContext)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	auto it{ mOpenAddrs.find(FileContext) };
	APP_ASSERT(it != mOpenAddrs.end());

	traceW(L"remove address=%p", FileContext);

	mOpenAddrs.erase(FileContext);
}

// EOF