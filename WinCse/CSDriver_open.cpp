#include "CSDriver.hpp"
#include <filesystem>

using namespace WCSE;


NTSTATUS CSDriver::DoOpen(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOpen);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName && PFileContext && FileInfo);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!this->shouldIgnoreFileName(FileName));

	//traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);

	FSP_FSCTL_FILE_INFO fileInfo;
	FileNameType fileNameType;

	const auto ntstatus = this->getFileInfoByFileName(START_CALLER FileName, &fileInfo, &fileNameType);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: getFileInfoByFileName, FileName=%s", FileName);
		return ntstatus;
	}

	// 念のため検査
	APP_ASSERT(fileInfo.LastWriteTime);

	// WinFsp に保存されるファイル・コンテキストを生成

	auto fc{ std::unique_ptr<PTFS_FILE_CONTEXT, void(*)(void*)>((PTFS_FILE_CONTEXT*)calloc(1, sizeof(PTFS_FILE_CONTEXT)), free) };
	if (!fc)
	{
		traceW(L"fault: calloc");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto fn{ std::unique_ptr<wchar_t, void(*)(void*)>(_wcsdup(FileName), free) };
	if (!fn)
	{
		traceW(L"fault: _wcsdup");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PTFS_FILE_CONTEXT* FileContext = fc.get();
	FileContext->FileName = fn.get();
	FileContext->FileInfo = fileInfo;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// go next

		APP_ASSERT(fileNameType == FileNameType::RootDirectory);
	}
	else
	{
		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (objKey.invalid())
		{
			traceW(L"invalid FileName=%s", FileName);

			return STATUS_OBJECT_NAME_INVALID;
		}

		// クラウド・ストレージのコンテキストを UParam に保存させる

		StatsIncr(_CallOpen);

		CSDeviceContext* ctx = mCSDevice->open(START_CALLER objKey, CreateOptions, GrantedAccess, FileContext->FileInfo);
		if (!ctx)
		{
			traceW(L"fault: open");

			//return STATUS_DEVICE_NOT_READY;
			return FspNtStatusFromWin32(ERROR_IO_DEVICE);
		}

		if (ctx->mObjKey.isObject())
		{
			traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);
		}

		FileContext->UParam = ctx;
	}

	// ゴミ回収対象に登録
	mResourceSweeper.add(FileContext);

	*PFileContext = FileContext;
	*FileInfo = fileInfo;

	fn.release();
	fc.release();

	return STATUS_SUCCESS;
}

VOID CSDriver::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);
	APP_ASSERT(FileContext);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		if (Flags & FspCleanupDelete)
		{
			NEW_LOG_BLOCK();

			traceW(L"FileName=%s, Flags=%lu", FileName, Flags);

			// setDelete() により削除フラグを設定されたファイルと、
			// CreateFile() 時に FILE_FLAG_DELETE_ON_CLOSE の属性が与えられたファイル
			// がクローズされるときにここを通過する

			bool b = mCSDevice->deleteObject(START_CALLER ctx->mObjKey);
			if (!b)
			{
				traceW(L"fault: deleteObject");
			}

			// WinFsp の Cleanup() で CloseHandle() しているので、同様の処理を行う
			//
			// --> ここで close() しておくことで、アップロードが必要かを簡単に判断できる

			ctx->mFile.close();

			// 閉じてしまうのだから、フラグもリセット

			ctx->mFlags = 0;
		}
	}
}

VOID CSDriver::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	StatsIncr(DoClose);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		APP_ASSERT(wcscmp(FileContext->FileName, L"\\") != 0);

		if (ctx->isFile())
		{
			traceW(L"FileName=%s, UParam=%p", FileContext->FileName, FileContext->UParam);
		}

		// クラウド・ストレージに UParam を解放させる

		StatsIncr(_CallClose);
		mCSDevice->close(START_CALLER ctx);

		{
			// 新規作成時に作成した一時メモリを解放

			std::lock_guard lock_(CreateNew.mGuard);

			const auto it = CreateNew.mFileInfos.find(FileContext->FileName);
			if (it != CreateNew.mFileInfos.cend())
			{
				traceW(L"erase CreateNew=%s", FileContext->FileName);
				CreateNew.mFileInfos.erase(it);
			}
		}

		delete ctx;
	}

	free(FileContext->FileName);

	FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);

	// ゴミ回収対象から削除
	mResourceSweeper.remove(FileContext);

	free(FileContext);
}

// EOF