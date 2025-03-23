#include "AwsS3.hpp"


using namespace WinCseLib;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
NTSTATUS AwsS3::readObject(CALLER_ARG WinCseLib::CSDeviceContext* ctx,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    StatsIncr(readObject);
    NEW_LOG_BLOCK();

    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    //return readObject_Simple(CONT_CALLER ctx, Buffer, Offset, Length, PBytesTransferred);
    return readObject_Multipart(CONT_CALLER ctx, Buffer, Offset, Length, PBytesTransferred);
}

NTSTATUS AwsS3::writeObject(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length,
    BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(writeObject);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    const auto remotePath{ ctx->getRemotePath() };

    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
        ctx, ctx->mFile.handle(), Offset, Length, remotePath.c_str());

    // ファイル名への参照を登録

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        if (ctx->mFile.invalid())
        {
            // openFile() 後の初回の呼び出し

            traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                ctx->mFile.handle(), Offset, Length, remotePath.c_str());

            APP_ASSERT(ctx->mGrantedAccess & FILE_WRITE_DATA);

            // 既存のファイルを開く

            ULONG CreateFlags = 0;
            //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // ディレクトリは操作しない

            if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

            ctx->mFile = ::CreateFileW(ctx->getFilePathW().c_str(),
                ctx->mGrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
                OPEN_EXISTING, CreateFlags, 0);

            if (ctx->mFile.invalid())
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return FspNtStatusFromWin32(lerr);
            }
        }
    }   // 名前のロックを解除 (safeShare の生存期間)

    const auto Handle = ctx->mFile.handle();

    if (ConstrainedIo)
    {
        LARGE_INTEGER FileSize;

        if (!::GetFileSizeEx(Handle, &FileSize))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

            return FspNtStatusFromWin32(lerr);
        }

        if (Offset >= (UINT64)FileSize.QuadPart)
        {
            return STATUS_SUCCESS;
        }

        if (Offset + Length > (UINT64)FileSize.QuadPart)
        {
            Length = (ULONG)((UINT64)FileSize.QuadPart - Offset);
        }
    }

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::WriteFile(Handle, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: WriteFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    return GetFileInfoInternal(Handle, FileInfo);
}

NTSTATUS AwsS3::remove(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext, BOOLEAN argDeleteFile)
{
    StatsIncr(remove);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    if (!ctx->mObjKey.hasKey())
    {
        traceW(L"fault: delete bucket");
        return STATUS_OBJECT_NAME_INVALID;
    }

    if (ctx->isDir())
    {
        DirInfoListType dirInfoList;

        if (!this->listObjects(CONT_CALLER ctx->mObjKey, &dirInfoList))
        {
            traceW(L"fault: listObjects");
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        const auto it = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
        {
            return wcscmp(dirInfo->FileNameBuf, L".") != 0 && wcscmp(dirInfo->FileNameBuf, L"..") != 0;
        });

        if (it != dirInfoList.end())
        {
            // 空でないディレクトリは削除不可
            // --> ".", ".." 以外のファイル/ディレクトリが存在する

            traceW(L"dir not empty");
            return STATUS_CANNOT_DELETE;
        }
    }

    {
        // S3 のファイルを削除

        Aws::S3::Model::DeleteObjectRequest request;
        request.SetBucket(ctx->mObjKey.bucketA());
        request.SetKey(ctx->mObjKey.keyA());
        const auto outcome = mClient.ptr->DeleteObject(request);

        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: DeleteObject");
            return STATUS_IO_DEVICE_ERROR;
        }

        // キャッシュ・メモリから削除

        const auto num = deleteCacheByObjKey(CONT_CALLER ctx->mObjKey);
        traceW(L"cache delete num=%d", num);
    }

    if (ctx->isFile())
    {
        // キャッシュ・ファイルを削除

        if (ctx->mFile.invalid())
        {
            APP_ASSERT(ctx->mGrantedAccess & DELETE);

            ULONG CreateFlags = 0;
            //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // ディレクトリは操作しない

            if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

            ctx->mFile = ::CreateFileW(ctx->getFilePathW().c_str(),
                ctx->mGrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
                OPEN_EXISTING, CreateFlags, 0);

            if (ctx->mFile.invalid())
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return FspNtStatusFromWin32(lerr);
            }
        }

        FILE_DISPOSITION_INFO DispositionInfo{};
        DispositionInfo.DeleteFile = argDeleteFile;

        if (!::SetFileInformationByHandle(ctx->mFile.handle(),
            FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFileInformationByHandle lerr=%lu", lerr);

            return FspNtStatusFromWin32(lerr);
        }

        traceW(L"success: SetFileInformationByHandle(DeleteFile=%s)", BOOL_CSTRW(argDeleteFile));
    }

	return STATUS_SUCCESS;
}

void AwsS3::cleanup(CALLER_ARG WinCseLib::CSDeviceContext* ctx, ULONG argFlags)
{
    StatsIncr(cleanup);
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());
    traceW(L"argFlags=%lu", argFlags);

    if (argFlags & FspCleanupDelete)
    {
        // WinFsp の Cleanup() で CloseHandle() しているので、同様の処理を行う

        ctx->mFile.close();
    }
}

// EOF