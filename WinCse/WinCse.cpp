#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>

using namespace WinCseLib;


WinCse::WinCse(WINCSE_DRIVER_STATS* argStats,
	const std::wstring& argTempDir, const std::wstring& argIniSection,
	IWorker* argDelayedWorker, IWorker* argIdleWorker, ICSDevice* argCSDevice)
	:
	mStats(argStats),
	mTempDir(argTempDir), mIniSection(argIniSection),
	mDelayedWorker(argDelayedWorker), mIdleWorker(argIdleWorker), mCSDevice(argCSDevice),
	mMaxFileSize(-1),
	mResourceRAII(this),
	mIgnoredFileNamePatterns{ LR"(\b(desktop\.ini|autorun\.inf|(eh)?thumbs\.db|AlbumArtSmall\.jpg|folder\.(ico|jpg|gif)|\.DS_Store)$)", std::regex_constants::icase }
{
	NEW_LOG_BLOCK();

	APP_ASSERT(std::filesystem::exists(argTempDir));
	APP_ASSERT(std::filesystem::is_directory(argTempDir));
	APP_ASSERT(argDelayedWorker);
	APP_ASSERT(argIdleWorker);
	APP_ASSERT(argCSDevice);
}

WinCse::~WinCse()
{
	NEW_LOG_BLOCK();

	traceW(L"close handle");

	mRefFile.close();
	mRefDir.close();

	traceW(L"all done.");
}

// EOF