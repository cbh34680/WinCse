#include "CSDriver.hpp"

using namespace WCSE;


NTSTATUS CSDriver::GetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
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

NTSTATUS CSDriver::GetSecurity(PTFS_FILE_CONTEXT* FileContext,
	PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize)
{
	StatsIncr(DoGetSecurity);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	if (wcscmp(FileContext->FileName, L"\\") != 0)
	{
		traceW(L"FileName=%s", FileContext->FileName);
	}

	const bool isDir = FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes);
	const HANDLE Handle = isDir ? mRefDir.handle() : mRefFile.handle();

	return HandleToSecurityInfo(Handle, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS CSDriver::Read(PTFS_FILE_CONTEXT* FileContext,
	PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
	StatsIncr(DoRead);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	traceW(L"FileName=%s, FileAttributes=%u, FileSize=%llu, Offset=%llu, Length=%lu",
		FileContext->FileName, FileContext->FileInfo.FileAttributes, FileContext->FileInfo.FileSize, Offset, Length);

	//APP_ASSERT(Offset <= FileContext->FileInfo.FileSize);

	// ファイルを作成する必要があるので、サイズ==0 でも readObject は呼び出す

	return mCSDevice->readObject(START_CALLER (CSDeviceContext*)FileContext->UParam,
		Buffer, Offset, Length, PBytesTransferred);
}

NTSTATUS CSDriver::ReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern,
	PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
	StatsIncr(DoReadDirectory);
    NEW_LOG_BLOCK();

    APP_ASSERT(FileContext && Buffer && PBytesTransferred);
	APP_ASSERT(FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));      // ディレクトリのみ

    std::optional<std::wregex> re;

    if (Pattern)
    {
        // 引数のパターンを正規表現に変換

        re = WildcardToRegexW(Pattern);
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

        if (dirInfoList.empty())
        {
            traceW(L"empty buckets");
            return STATUS_SUCCESS;
        }
    }
    else
    {
        // "\bucket" または "\bucket\key"

        const auto objKey{ ObjectKey::fromWinPath(FileName) };
        if (objKey.invalid())
        {
            traceW(L"invalid FileName=%s", FileName);

            return STATUS_OBJECT_NAME_INVALID;
        }

        const auto listObjKey{ objKey.toDir() };

        if (listObjKey.isObject())
        {
            traceW(L"listObjKey=%s", listObjKey.c_str());
        }

        // キーが空の場合)		bucket & ""     で検索
        // キーが空でない場合)	bucket & "key/" で検索

        if (!mCSDevice->listDisplayObjects(START_CALLER listObjKey, &dirInfoList))
        {
            traceW(L"not found/2");

            return STATUS_OBJECT_NAME_INVALID;
        }

        // 少なくとも "." はあるので空ではないはず

        //traceW(L"object count: %zu", dirInfoList.size());
    }

    APP_ASSERT(!dirInfoList.empty());

    // 取得したものを WinFsp に転送する

    NTSTATUS DirBufferResult = STATUS_SUCCESS;

    if (FspFileSystemAcquireDirectoryBuffer(&FileContext->DirBuffer, 0 == Marker, &DirBufferResult))
    {
        for (const auto& dirInfo: dirInfoList)
        {
            if (this->shouldIgnoreFileName(dirInfo->FileNameBuf))
            {
                continue;
            }

            if (re)
            {
                if (!std::regex_match(dirInfo->FileNameBuf, *re))
                {
                    continue;
                }
            }

            if (!FspFileSystemFillDirectoryBuffer(&FileContext->DirBuffer, dirInfo->data(), &DirBufferResult))
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

    return STATUS_SUCCESS;
}

// EOF
