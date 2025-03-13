#include "AwsS3.hpp"

using namespace WinCseLib;

void AwsS3::cleanup(CALLER_ARG WinCseLib::IOpenContext* argOpenContext, ULONG argFlags)
{
    StatsIncr(cleanup);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);
    APP_ASSERT(ctx);

    if (argFlags & FspCleanupDelete)
    {
        // WinFsp の Cleanup() で CloseHandle() しているので、同様の処理を行う

        ctx->closeLocalFile();
    }
}

NTSTATUS AwsS3::remove(CALLER_ARG WinCseLib::IOpenContext* argOpenContext, BOOLEAN argDeleteFile)
{
    StatsIncr(remove);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);
    APP_ASSERT(ctx);

    NTSTATUS ntstatus = STATUS_IO_DEVICE_ERROR;

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    if (mReadonlyFilesystem)
    {
        // ここは通過しない
        // おそらくシェルで削除操作が止められている

        traceW(L"readonly filesystem");
        goto exit;
    }

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

            ntstatus = STATUS_CANNOT_DELETE;

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

        if (!ctx->openLocalFile(0, OPEN_ALWAYS))
        {
            traceW(L"fault: openLocalFile");
            goto exit;
        }

        FILE_DISPOSITION_INFO DispositionInfo{};

        DispositionInfo.DeleteFile = argDeleteFile;

        if (!::SetFileInformationByHandle(ctx->mLocalFile,
            FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
        {
            traceW(L"fault: SetFileInformationByHandle");
            goto exit;
        }

        traceW(L"success: SetFileInformationByHandle(DeleteFile=%s)", argDeleteFile ? L"true" : L"false");
    }

    ntstatus = STATUS_SUCCESS;

exit:
    traceW(L"ntstatus=%ld", ntstatus);

	return ntstatus;
}


// EOF