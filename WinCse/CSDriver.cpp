#include "WinCseLib.h"
#include "CSDriver.hpp"
#include <filesystem>


using namespace WCSE;

static PCWSTR const DEFAULT_IGNORE_PATTERNS = LR"(\b(desktop\.ini|autorun\.inf|(eh)?thumbs\.db|AlbumArtSmall\.jpg|folder\.(ico|jpg|gif)|\.DS_Store)$)";

CSDriver::CSDriver(WINCSE_DRIVER_STATS* argStats,
	const std::wstring& argTempDir, const std::wstring& argIniSection,
	NamedWorker argWorkers[], ICSDevice* argCSDevice)
	:
	mStats(argStats),
	mTempDir(argTempDir), mIniSection(argIniSection),
	mCSDevice(argCSDevice),
	mResourceSweeper(this),
	mIgnoreFileNamePatterns{ DEFAULT_IGNORE_PATTERNS, std::regex_constants::icase }
{
	NEW_LOG_BLOCK();

	APP_ASSERT(std::filesystem::exists(argTempDir));
	APP_ASSERT(std::filesystem::is_directory(argTempDir));
	APP_ASSERT(argCSDevice);

	NamedWorkersToMap(argWorkers, &mWorkers);
}

CSDriver::~CSDriver()
{
	NEW_LOG_BLOCK();

	traceW(L"all done.");
}

bool CSDriver::shouldIgnoreFileName(const std::wstring& arg) const noexcept
{
	// desktop.ini �Ȃǃ��N�G�X�g�������߂�����͖̂�������

	if (mIgnoreFileNamePatterns.mark_count() == 0)
	{
		// ���K�\�����ݒ肳��Ă��Ȃ�
		return false;
	}

	return std::regex_search(arg, mIgnoreFileNamePatterns);
}

//
// �G�N�X�v���[���[���J�����܂ܐؒf����� WinFsp �� Close �����s����Ȃ� (�ׂ��Ǝv��)
// �̂ŁADoOpen ���Ă΂�� DoClose ���Ă΂�Ă��Ȃ����̂́A�A�v���P�[�V�����I������
// �����I�� DoClose ���Ăяo��
// 
// ���u���Ă����͂Ȃ����A�f�o�b�O���Ƀ��������[�N�Ƃ��ĕ񍐂���Ă��܂�
// �{���̈Ӗ��ł̃��������[�N�ƍ��݂��Ă��܂�����
//
CSDriver::ResourceSweeper::~ResourceSweeper()
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
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }

void CSDriver::ResourceSweeper::add(PTFS_FILE_CONTEXT* FileContext)
{
	THREAD_SAFE();
	APP_ASSERT(mOpenAddrs.find(FileContext) == mOpenAddrs.end());

	mOpenAddrs.insert(FileContext);
}

void CSDriver::ResourceSweeper::remove(PTFS_FILE_CONTEXT* FileContext)
{
	THREAD_SAFE();
	APP_ASSERT(mOpenAddrs.find(FileContext) != mOpenAddrs.end());

	mOpenAddrs.erase(FileContext);
}

// EOF