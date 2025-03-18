#include "AwsS3.hpp"


using namespace WinCseLib;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
bool AwsS3::readObject(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    StatsIncr(readObject);
    NEW_LOG_BLOCK();

    CSDeviceContext* ctx = dynamic_cast<CSDeviceContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    //return readObject_Simple(CONT_CALLER ctx, Buffer, Offset, Length, PBytesTransferred);
    return readObject_Multipart(CONT_CALLER ctx, Buffer, Offset, Length, PBytesTransferred);
}

void AwsS3::cleanup(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext, ULONG argFlags)
{
    StatsIncr(cleanup);
    NEW_LOG_BLOCK();

    CSDeviceContext* ctx = dynamic_cast<CSDeviceContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);

    if (argFlags & FspCleanupDelete)
    {
        // WinFsp の Cleanup() で CloseHandle() しているので、同様の処理を行う

        ctx->closeLocalFile();
    }
}

bool AwsS3::remove(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext, BOOLEAN argDeleteFile)
{
    StatsIncr(remove);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    bool ret = false;

    if (!ctx->mObjKey.hasKey())
    {
        traceW(L"fault: delete bucket");
        goto exit;
    }

    if (ctx->isDir())
    {
        DirInfoListType dirInfoList;

        if (!this->listObjects(CONT_CALLER ctx->mObjKey, &dirInfoList))
        {
            traceW(L"fault: listObjects");
            goto exit;
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
            goto exit;
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
            goto exit;
        }

        // キャッシュ・メモリから削除

        const auto num = deleteCacheByObjKey(CONT_CALLER ctx->mObjKey);
        traceW(L"cache delete num=%d", num);
    }

    if (ctx->isFile())
    {
        // キャッシュ・ファイルを削除

        APP_ASSERT(ctx->mGrantedAccess & DELETE);

        if (!ctx->openLocalFile(0, OPEN_ALWAYS))
        {
            traceW(L"fault: openLocalFile");
            goto exit;
        }

        FILE_DISPOSITION_INFO DispositionInfo{};

        DispositionInfo.DeleteFile = argDeleteFile;

        if (!::SetFileInformationByHandle(ctx->mLocalFile.handle(),
            FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
        {
            traceW(L"fault: SetFileInformationByHandle");
            goto exit;
        }

        traceW(L"success: SetFileInformationByHandle(DeleteFile=%s)", BOOL_CSTRW(argDeleteFile));
    }

    ret = true;

exit:
    traceW(L"ret = %s", BOOL_CSTRW(ret));

	return ret;
}

bool AwsS3::writeObject(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length,
    BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(writeObject);
    NEW_LOG_BLOCK();

    CSDeviceContext* ctx = dynamic_cast<CSDeviceContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    bool ret = false;
    auto Handle = ctx->mLocalFile.handle();
    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    LARGE_INTEGER FileSize{};
    OVERLAPPED Overlapped{};

    if (ConstrainedIo)
    {
        if (!::GetFileSizeEx(Handle, &FileSize))
        {
            ntstatus = FspNtStatusFromWin32(GetLastError());
            goto exit;
        }

        if (Offset >= (UINT64)FileSize.QuadPart)
        {
            ret = true;
            goto exit;
        }

        if (Offset + Length > (UINT64)FileSize.QuadPart)
        {
            Length = (ULONG)((UINT64)FileSize.QuadPart - Offset);
        }
    }

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::WriteFile(Handle, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        ntstatus = FspNtStatusFromWin32(GetLastError());
        goto exit;
    }

    ntstatus = GetFileInfoInternal(Handle, FileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    ret = true;

exit:
    traceW(L"ret = %s", BOOL_CSTRW(ret));

    return ret;
}

// EOF