#include "WinCseLib.h"
#include "WinCse.hpp"

using namespace WinCseLib;


NTSTATUS WinCse::DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoGetFileInfo);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);

	if (wcscmp(FileContext->FileName, L"\\") != 0)
	{
		traceW(L"FileName=%s", FileContext->FileName);
	}

	*FileInfo = FileContext->FileInfo;

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	StatsIncr(DoGetSecurity);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	if (wcscmp(FileContext->FileName, L"\\") != 0)
	{
		traceW(L"FileName=%s", FileContext->FileName);
	}

	const bool isDir = FA_IS_DIR(FileContext->FileInfo.FileAttributes);
	const HANDLE Handle = isDir ? mRefDir.handle() : mRefFile.handle();

	return HandleToSecurityInfo(Handle, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoRead(PTFS_FILE_CONTEXT* FileContext,
	PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
	StatsIncr(DoRead);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred);
	APP_ASSERT(!FA_IS_DIR(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	traceW(L"FileName=%s, FileAttributes=%u, Size=%llu, Offset=%llu",
		FileContext->FileName, FileContext->FileInfo.FileAttributes, FileContext->FileInfo.FileSize, Offset);

	//APP_ASSERT(Offset <= FileContext->FileInfo.FileSize);

	// ファイルを作成する必要があるので、サイズ==0 でも readObject は呼び出す

	return mCSDevice->readObject(START_CALLER (CSDeviceContext*)FileContext->UParam,
		Buffer, Offset, Length, PBytesTransferred);
}

NTSTATUS WinCse::DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern,
	PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
	StatsIncr(DoReadDirectory);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred);

	if (wcscmp(FileContext->FileName, L"\\") != 0)
	{
		traceW(L"FileName=%s", FileContext->FileName);
	}

	APP_ASSERT(FA_IS_DIR(FileContext->FileInfo.FileAttributes));

	std::wregex re;
	std::wregex* pRe = nullptr;

	if (Pattern)
	{
		const auto pattern = WildcardToRegexW(Pattern);
		re = std::wregex(pattern);
		pRe = &re;
	}

	// ディレクトリの中の一覧取得

	PCWSTR FileName = FileContext->FileName;

	DirInfoListType dirInfoList;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" へのアクセスはバケット一覧を提供

		if (!mCSDevice->listBuckets(START_CALLER &dirInfoList))
		{
			traceW(L"not fouund/1");

			return STATUS_OBJECT_NAME_INVALID;
		}

		APP_ASSERT(!dirInfoList.empty());
		//traceW(L"bucket count: %zu", dirInfoList.size());
	}
	else
	{
		// "\bucket" または "\bucket\key"

		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (!objKey.valid())
		{
			traceW(L"invalid FileName=%s", FileName);

			return STATUS_OBJECT_NAME_INVALID;
		}

		// キーが空の場合)		bucket & ""     で検索
		// キーが空でない場合)	bucket & "key/" で検索

		if (!mCSDevice->listObjects(START_CALLER objKey.toDir(), &dirInfoList))
		{
			traceW(L"not found/2");

			return STATUS_OBJECT_NAME_INVALID;
		}

		APP_ASSERT(!dirInfoList.empty());
		//traceW(L"object count: %zu", dirInfoList.size());
	}

	if (!dirInfoList.empty())
	{
		// 取得したものを WinFsp に転送する

		NTSTATUS DirBufferResult = STATUS_SUCCESS;

		if (FspFileSystemAcquireDirectoryBuffer(&FileContext->DirBuffer, 0 == Marker, &DirBufferResult))
		{
			for (const auto& dirInfo: dirInfoList)
			{
				if (shouldIgnoreFileName(dirInfo->FileNameBuf))
				{
					continue;
				}

				if (pRe)
				{
					if (!std::regex_match(dirInfo->FileNameBuf, *pRe))
					{
						continue;
					}
				}

				if (!FspFileSystemFillDirectoryBuffer(&FileContext->DirBuffer, dirInfo.get(), &DirBufferResult))
				{
					break;
				}
			}

			FspFileSystemReleaseDirectoryBuffer(&FileContext->DirBuffer);
		}

		if (!NT_SUCCESS(DirBufferResult))
		{
			return DirBufferResult;
		}

		FspFileSystemReadDirectoryBuffer(&FileContext->DirBuffer,
			Marker, Buffer, BufferLength, PBytesTransferred);
	}

	return STATUS_SUCCESS;
}

// EOF
