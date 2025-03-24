#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>
#include <sstream>

using namespace WinCseLib;


NTSTATUS WinCse::DoCreate(const wchar_t* FileName,
	UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoCreate);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!mReadonlyVolume);		// おそらくシェルで削除操作が止められている

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"CreateOptions=%u, GrantedAccess=%u, FileAttributes=%u, SecurityDescriptor=%p, AllocationSize=%llu, PFileContext=%p, FileInfo=%p",
		CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

	const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
	NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;
	PTFS_FILE_CONTEXT* FileContext = nullptr;
	FSP_FSCTL_FILE_INFO fileInfo{};

	if (isFileNameIgnored(FileName))
	{
		ntstatus = STATUS_OBJECT_NAME_INVALID;
		goto exit;
	}

	if (!objKey.valid())
	{
		traceW(L"illegal FileName: \"%s\"", FileName);
		ntstatus = STATUS_OBJECT_NAME_INVALID;
		goto exit;
	}

	traceW(L"objKey=%s", objKey.str().c_str());

	if (!mCSDevice->headBucket(START_CALLER objKey.bucket()))
	{
		// "\\" に新規作成しようとしたとき
		//
		// 意味的にバケットの作成になるので、拒否

		traceW(L"fault: headBucket");
		goto exit;

		//return STATUS_ACCESS_DENIED;				// 理想的なメッセージだが、何度も呼び出される
		//return STATUS_INVALID_PARAMETER;			// 変なメッセージになる
		//return STATUS_NOT_IMPLEMENTED;			// 無効な MS-DOS ファンクション (何度も呼び出されてしまう)
	}

	if (objKey.meansFile())
	{
		if (mCSDevice->headObject(START_CALLER objKey.toDir(), nullptr))
		{
			// ファイル名と同じディレクトリが存在するとき
			traceW(L"fault: already exists same name dir");

			ntstatus = STATUS_OBJECT_NAME_COLLISION;
			goto exit;
		}
	}

	//
	FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
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

	if (objKey.hasKey())
	{
		// クラウド・ストレージのコンテキストを UParam に保存させる

		StatsIncr(_CallCreate);

		CSDeviceContext* ctx = mCSDevice->create(START_CALLER objKey,
			CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, &fileInfo);

		if (!ctx)
		{
			traceW(L"fault: create");
			ntstatus = STATUS_DEVICE_NOT_READY;
			goto exit;
		}

		FileContext->UParam = ctx;
	}
	else
	{
		// "\\bucket" に対する create --> ディレクトリ

		ntstatus = GetFileInfoInternal(mRefDir.handle(), &fileInfo);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: GetFileInfoInternal");
			goto exit;
		}
	}

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

NTSTATUS WinCse::DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOpen);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName && PFileContext && FileInfo);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!isFileNameIgnored(FileName));

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", CreateOptions, GrantedAccess, PFileContext, FileInfo);

	PTFS_FILE_CONTEXT* FileContext = nullptr;
	FSP_FSCTL_FILE_INFO fileInfo{};
	NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

	ntstatus = FileNameToFileInfo(START_CALLER FileName, &fileInfo);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: FileNameToFileInfo");
		goto exit;
	}

	// 念のため検査
	APP_ASSERT(fileInfo.LastWriteTime);

	// WinFsp に保存されるファイル・コンテキストを生成

	FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
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

	if (wcscmp(FileName, L"\\") == 0)
	{
		traceW(L"root access");

		//APP_ASSERT(fileInfo.FileSize == 0);
	}
	else
	{
		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };

		if (!objKey.valid())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);
			ntstatus = STATUS_OBJECT_NAME_INVALID;
			goto exit;
		}

		if (FA_IS_DIR(fileInfo.FileAttributes))
		{
			// ディレクトリへのアクセス

			//APP_ASSERT(fileInfo.FileSize == 0);
		}
		else
		{
			// ファイルへのアクセス

			APP_ASSERT(objKey.hasKey());

			traceW(L"FileSize: %llu", fileInfo.FileSize);

			if (mMaxFileSize > 0)
			{
				if (fileInfo.FileSize > (FILESIZE_1MiBu * mMaxFileSize))
				{
					ntstatus = STATUS_DEVICE_NOT_READY;
					traceW(L"%llu: When a file size exceeds the maximum size that can be opened.", fileInfo.FileSize);
					goto exit;
				}
			}
		}

		// クラウド・ストレージのコンテキストを UParam に保存させる

		StatsIncr(_CallOpen);

		CSDeviceContext* ctx = mCSDevice->open(START_CALLER objKey, CreateOptions, GrantedAccess, fileInfo);
		if (!ctx)
		{
			traceW(L"fault: open");
			ntstatus = STATUS_DEVICE_NOT_READY;
			goto exit;
		}

		FileContext->UParam = ctx;
	}

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

VOID WinCse::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"(FileContext)FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Flags=%lu", Flags);

	mCSDevice->cleanup(START_CALLER (CSDeviceContext*)FileContext->UParam, Flags);
}

NTSTATUS WinCse::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	StatsIncr(DoClose);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"UParam=%p", FileContext->UParam);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;

	if (ctx)
	{
		APP_ASSERT(wcscmp(FileContext->FileName, L"\\") != 0);

		// クラウド・ストレージに UParam を解放させる

		StatsIncr(_CallClose);
		mCSDevice->close(START_CALLER ctx);
	}

	free(FileContext->FileName);

	FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);

	// ゴミ回収対象から削除
	mResourceSweeper.remove(FileContext);

	free(FileContext);

	return STATUS_SUCCESS;
}

// EOF