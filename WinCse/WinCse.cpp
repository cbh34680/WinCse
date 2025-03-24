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
// エクスプローラーを開いたまま切断すると WinFsp の Close が実行されない (為だと思う)
// ので、DoOpen が呼ばれて DoClose が呼ばれていないものは、アプリケーション終了時に
// 強制的に DoClose を呼び出す
// 
// 放置しても問題はないが、デバッグ時にメモリリークとして報告されてしまい
// 本来の意味でのメモリリークと混在してしまうため
//
WinCse::ResourceSweeper::~ResourceSweeper()
{
	NEW_LOG_BLOCK();

	// DoClose で mOpenAddrs.erase() をするのでコピーが必要

	auto copy{ mOpenAddrs };

	for (auto& FileContext: copy)
	{
		::InterlockedIncrement(&(mThat->mStats->_ForceClose));

		traceW(L"force close address=%p", FileContext);

		mThat->DoClose(FileContext);
	}
}

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
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