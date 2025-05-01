#include "FileContextSweeper.hpp"

using namespace CSELIB;
using namespace CSEDRV;

//
// エクスプローラーを開いたまま切断すると WinFsp の Close が実行されない (為だと思う)
// ので、RelayOpen が呼ばれて RelayClose が呼ばれていないものは、アプリケーション終了時に
// 強制的に RelayClose を呼び出す
// 
// 放置しても問題はないが、デバッグ時にメモリリークとして報告されてしまい
// 本来の意味でのメモリリークと混在してしまうため
//

FileContextSweeper::~FileContextSweeper()
{
	NEW_LOG_BLOCK();

	// RelayClose で mOpenAddrs.erase() をするのでコピーが必要

	auto copy{ mOpenAddrs };

	for (auto* addr: copy)
	{
		traceW(L"force close address=%p", addr);

		delete addr;
	}
}

#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ mGuard }

void FileContextSweeper::add(FileContext* ctx) noexcept
{
	THREAD_SAFE();
	APP_ASSERT(mOpenAddrs.find(ctx) == mOpenAddrs.cend());

	mOpenAddrs.insert(ctx);
}

void FileContextSweeper::remove(FileContext* ctx) noexcept
{
	THREAD_SAFE();
	APP_ASSERT(mOpenAddrs.find(ctx) != mOpenAddrs.cend());

	mOpenAddrs.erase(ctx);
}

// EOF