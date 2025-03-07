#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>
#include <sstream>

using namespace WinCseLib;

#undef traceA


WinCse::WinCse(const std::wstring& argTempDir, const std::wstring& argIniSection,
	IWorker* argDelayedWorker, IWorker* argIdleWorker, ICSDevice* argCSDevice) :
	mTempDir(argTempDir), mIniSection(argIniSection),
	mDelayedWorker(argDelayedWorker), mIdleWorker(argIdleWorker), mCSDevice(argCSDevice),
	mMaxFileSize(-1),
	mIgnoredFileNamePatterns{ LR"(.*\\(desktop\.ini|autorun\.inf|thumbs\.db|\.DS_Store)$)", std::regex_constants::icase }
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

	if (mFileRefHandle != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(mFileRefHandle);
		mFileRefHandle = INVALID_HANDLE_VALUE;
	}

	if (mDirRefHandle != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(mDirRefHandle);
		mDirRefHandle = INVALID_HANDLE_VALUE;
	}

	traceW(L"all done.");
}

NTSTATUS WinCse::DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOpen);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!isFileNameIgnored(FileName));
	APP_ASSERT(PFileContext);
	APP_ASSERT(FileInfo);

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", CreateOptions, GrantedAccess, PFileContext, FileInfo);

	PTFS_FILE_CONTEXT* FileContext = nullptr;
	FSP_FSCTL_FILE_INFO fileInfo = {};
	NTSTATUS Result = STATUS_UNSUCCESSFUL;

	Result = FileNameToFileInfo(START_CALLER FileName, &fileInfo);
	if (!NT_SUCCESS(Result))
	{
		traceW(L"fault: FileNameToFileInfo");
		goto exit;
	}

	// 念のため検査
	//APP_ASSERT(fileInfo.FileAttributes);
	APP_ASSERT(fileInfo.CreationTime);

	// WinFsp に保存されるファイル・コンテキストを生成
	// このメモリは WinFsp の Close() で削除されるため解放不要

	FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof *FileContext);
	if (!FileContext)
	{
		traceW(L"fault: allocate FileContext");
		Result = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->FileName = _wcsdup(FileName);
	if (!FileContext->FileName)
	{
		traceW(L"fault: allocate FileContext->OpenFileName");
		Result = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	if (wcscmp(FileName, L"\\") == 0)
	{
		traceW(L"root access");

		APP_ASSERT(fileInfo.FileSize == 0);
	}
	else
	{
		const BucketKey bk{ FileName };

		if (!bk.OK())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);
			return STATUS_INVALID_PARAMETER;
		}

		if (fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// ディレクトリへのアクセス

			APP_ASSERT(fileInfo.FileSize == 0);
		}
		else
		{
			// ファイルへのアクセス

			APP_ASSERT(bk.hasKey());

			// マルチパート処理次第で最大ファイル・サイズの制限をなくす

			traceW(L"FileSize: %llu", fileInfo.FileSize);

			if (mMaxFileSize > 0)
			{
				if (fileInfo.FileSize > 1024ULL * 1024 * mMaxFileSize)
				{
					Result = STATUS_DEVICE_NOT_READY;
					traceW(L"%llu: When a file size exceeds the maximum size that can be opened.", fileInfo.FileSize);
					goto exit;
				}
			}

			// クラウド・ストレージのコンテキストを UParam に保存させる

			if (!mCSDevice->openFile(START_CALLER
				bk.bucket(), bk.key(), CreateOptions, GrantedAccess, fileInfo, &FileContext->UParam))
			{
				traceW(L"fault: openFile");
				Result = STATUS_DEVICE_NOT_READY;
				goto exit;
			}
		}
	}

	FileContext->FileInfo = fileInfo;

	// SUCSESS RETURN

	*PFileContext = FileContext;
	FileContext = nullptr;

	*FileInfo = fileInfo;

exit:
	if (FileContext)
	{
		free(FileContext->FileName);
	}
	free(FileContext);

	traceW(L"return %ld", Result);

	return Result;
}

NTSTATUS WinCse::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	StatsIncr(DoClose);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"Open.FileName: \"%s\"", FileContext->FileName);

	if (FileContext->UParam)
	{
		// クラウド・ストレージに UParam を解放させる

		StatsIncr(_CallCloseFile);
		mCSDevice->closeFile(START_CALLER FileContext->UParam);
	}

	free(FileContext->FileName);

	// FileContext は呼び出し元で free している

	return STATUS_SUCCESS;
}

// EOF