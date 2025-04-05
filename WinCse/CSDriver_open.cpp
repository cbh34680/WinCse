#include "WinCseLib.h"
#include "CSDriver.hpp"
#include <filesystem>
#include <sstream>

using namespace WCSE;


NTSTATUS CSDriver::DoCreate(PCWSTR FileName,
	UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoCreate);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!mReadonlyVolume);		// おそらくシェルで削除操作が止められている

	traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, FileAttributes=%u, SecurityDescriptor=%p, AllocationSize=%llu, PFileContext=%p, FileInfo=%p",
		FileName, CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

	if (shouldIgnoreFileName(FileName))
	{
		// "desktop.ini" などは無視させる

		traceW(L"ignore pattern");
		return STATUS_OBJECT_NAME_INVALID;
	}

	const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
	if (objKey.invalid())
	{
		traceW(L"invalid FileName=%s", FileName);

		return STATUS_OBJECT_NAME_INVALID;
	}

	traceW(L"objKey=%s", objKey.str().c_str());

	if (objKey.isBucket())
	{
		// バケットに対する create の実行

		//return STATUS_ACCESS_DENIED;
		//return FspNtStatusFromWin32(ERROR_ACCESS_DENIED);
		//return FspNtStatusFromWin32(ERROR_WRITE_PROTECT);
		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	APP_ASSERT(objKey.hasKey());

	if (CreateOptions & FILE_DIRECTORY_FILE)
	{
		// "ディレクトリ" のとき
		// 
		// --> 同名のファイルを検索

		if (mCSDevice->headObject_File(START_CALLER objKey, nullptr))
		{
			// 同じ名前の "ファイル" が存在する

			traceW(L"fault: exists same name");
			return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		}
	}
	else
	{
		// "ファイル" のとき
		//
		// --> 同名のディレクトリを検索

		if (mCSDevice->headObject_Dir(START_CALLER objKey.toDir(), nullptr))
		{
			// 同じ名前の "ディレクトリ" が存在する

			traceW(L"fault: exists same name");
			return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		}
	}

	const ObjectKey createObjKey{ CreateOptions & FILE_DIRECTORY_FILE ? objKey.toDir() : objKey };

	if (mCSDevice->headObject(START_CALLER createObjKey, nullptr))
	{
		// 同じ名前のものが存在するとき

		traceW(L"fault: exists same name");
		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	//
	// 空のオブジェクトを作成して、その情報を fileInfo に記録する
	//

	const auto now = GetCurrentWinFileTime100ns();

	FSP_FSCTL_FILE_INFO fileInfo{};
	fileInfo.FileAttributes = FileAttributes;
	fileInfo.CreationTime = now;
	fileInfo.LastAccessTime = now;
	fileInfo.LastWriteTime = now;

	if (!mCSDevice->putObject(START_CALLER createObjKey, fileInfo, nullptr))
	{
		traceW(L"fault: putObject");

		//return STATUS_DEVICE_NOT_READY;
		return FspNtStatusFromWin32(ERROR_IO_DEVICE);
	}

	if (CreateOptions & FILE_DIRECTORY_FILE)
	{
		// ディレクトリの場合
		//
		// --> 誰か作ったものでも気にする必要はない
	}
	else
	{
		// ファイルの場合

		FSP_FSCTL_FILE_INFO checkFileInfo;

		if (!mCSDevice->headObject_File(START_CALLER createObjKey, &checkFileInfo))
		{
			traceW(L"fault: headObject_File");
			return FspNtStatusFromWin32(ERROR_IO_DEVICE);
		}

		if (fileInfo.CreationTime == checkFileInfo.CreationTime &&
			fileInfo.LastWriteTime == checkFileInfo.LastWriteTime &&
			fileInfo.FileSize == checkFileInfo.FileSize)
		{
			// go next
		}
		else
		{
			// putObject() した状態と異なっているので、他でアップロードしたものと判定
			// --> 他のプロセスによってファイルが使用中

			return FspNtStatusFromWin32(ERROR_SHARING_VIOLATION);
		}
	}

	//
	NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;
	CSDeviceContext* ctx = nullptr;

	PTFS_FILE_CONTEXT* FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
	if (0 == FileContext)
	{
		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->FileName = _wcsdup(FileName);
	if (!FileContext->FileName)
	{
		traceW(L"fault: _wcsdup");

		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	// リソースを作成し UParam に保存

	StatsIncr(_CallCreate);

	ctx = mCSDevice->create(START_CALLER createObjKey, fileInfo,
		CreateOptions, GrantedAccess, FileAttributes);

	if (!ctx)
	{
		traceW(L"fault: create");

		//ntstatus = STATUS_DEVICE_NOT_READY;
		ntstatus = FspNtStatusFromWin32(ERROR_IO_DEVICE);
		goto exit;
	}

	FileContext->UParam = ctx;

	FileContext->FileInfo = fileInfo;

	// ゴミ回収対象に登録
	mResourceSweeper.add(FileContext);

	*PFileContext = FileContext;
	FileContext = nullptr;

	*FileInfo = fileInfo;

	ntstatus = STATUS_SUCCESS;

exit:
	if (FileContext)
	{
		free(FileContext->FileName);
	}
	free(FileContext);

	traceW(L"return NTSTATUS=%ld", ntstatus);

	return ntstatus;
}

NTSTATUS CSDriver::DoOpen(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOpen);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName && PFileContext && FileInfo);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!shouldIgnoreFileName(FileName));

	//traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);

	FSP_FSCTL_FILE_INFO fileInfo{};

	NTSTATUS ntstatus = FileNameToFileInfo(START_CALLER FileName, &fileInfo);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: FileNameToFileInfo, FileName=%s", FileName);
		return ntstatus;
	}

	// 念のため検査
	APP_ASSERT(fileInfo.LastWriteTime);

	// WinFsp に保存されるファイル・コンテキストを生成

	PTFS_FILE_CONTEXT* FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
	if (!FileContext)
	{
		traceW(L"fault: calloc");

		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->FileName = _wcsdup(FileName);
	if (!FileContext->FileName)
	{
		traceW(L"fault: _wcsdup");

		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->FileInfo = fileInfo;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// go next
	}
	else
	{
		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (objKey.invalid())
		{
			traceW(L"invalid FileName=%s", FileName);

			ntstatus = STATUS_OBJECT_NAME_INVALID;
			goto exit;
		}

		// クラウド・ストレージのコンテキストを UParam に保存させる

		StatsIncr(_CallOpen);

		CSDeviceContext* ctx = mCSDevice->open(START_CALLER objKey, CreateOptions, GrantedAccess, FileContext->FileInfo);
		if (!ctx)
		{
			traceW(L"fault: open");

			//ntstatus = STATUS_DEVICE_NOT_READY;
			ntstatus = FspNtStatusFromWin32(ERROR_IO_DEVICE);
			goto exit;
		}

		if (!ctx->mObjKey.isBucket())
		{
			traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);
		}

		FileContext->UParam = ctx;
	}

	// ゴミ回収対象に登録
	mResourceSweeper.add(FileContext);

	*PFileContext = FileContext;
	FileContext = nullptr;

	*FileInfo = fileInfo;

	ntstatus = STATUS_SUCCESS;

exit:
	if (FileContext)
	{
		free(FileContext->FileName);
	}
	free(FileContext);

	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"return NTSTATUS=%ld", ntstatus);
	}

	return ntstatus;
}

VOID CSDriver::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		traceW(L"FileName=%s, Flags=%lu", FileName, Flags);

		if (Flags & FspCleanupDelete)
		{
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

		//if (!ctx->mObjKey.isBucket())
		if (ctx->isFile())
		{
			traceW(L"FileName=%s, UParam=%p", FileContext->FileName, FileContext->UParam);
		}

		// クラウド・ストレージに UParam を解放させる

		StatsIncr(_CallClose);
		mCSDevice->close(START_CALLER ctx);

		delete ctx;
	}

	free(FileContext->FileName);

	FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);

	// ゴミ回収対象から削除
	mResourceSweeper.remove(FileContext);

	free(FileContext);
}

// EOF