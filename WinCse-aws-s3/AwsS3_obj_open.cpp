#include "AwsS3.hpp"


using namespace WinCseLib;


CSDeviceContext* AwsS3::create(CALLER_ARG const ObjectKey& argObjKey,
    const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 argFileAttributes,
    FSP_FSCTL_FILE_INFO* pFileInfo)
{
    StatsIncr(create);
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.hasKey());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    FSP_FSCTL_FILE_INFO fileInfo{};
    CreateContext* ctx = nullptr;

    if (CreateOptions & FILE_DIRECTORY_FILE)
    {
        // create s3 directory
        const auto objKey{ argObjKey.toDir() };

        if (!headObject(CONT_CALLER objKey, nullptr))
        {
            // 存在しなければ作成

            Aws::S3::Model::PutObjectRequest request;
            request.SetBucket(objKey.bucketA());
            request.SetKey(objKey.keyA());

            const auto outcome = mClient.ptr->PutObject(request);

            if (!outcomeIsSuccess(outcome))
            {
                traceW(L"fault: PutObject");
                goto exit;
            }

            // キャッシュ・メモリから削除
            //
            // 後続の処理で DoGetSecurityByName() が呼ばれるが、上記で作成したディレクトリが
            // キャッシュに反映されていない状態で利用されてしまうことを回避するために
            // 事前に削除しておき、改めてキャッシュを作成させる

            const auto num = deleteCacheByObjKey(CONT_CALLER objKey);
            traceW(L"cache delete num=%d", num);
        }

        // ディレクトリ参照用の情報を設定

        ntstatus = GetFileInfoInternal(mRefDir.handle(), &fileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            goto exit;
        }
    }
    else
    {
        UINT32 FileAttributes = argFileAttributes;
        ULONG CreateFlags = 0;

        const auto loclPath{ mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(argObjKey.str()) };
        traceW(L"argObjKey=%s localPath=%s", argObjKey.c_str(), loclPath.c_str());

        CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;

        if (CreateOptions & FILE_DELETE_ON_CLOSE)
        {
            CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
        }

        FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

        if (FileAttributes == 0)
        {
            FileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

        hFile = ::CreateFileW
        (
            loclPath.c_str(),
            GrantedAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            CREATE_ALWAYS,
            CreateFlags | FileAttributes,
            NULL
        );

        if (hFile == INVALID_HANDLE_VALUE)
        {
            traceW(L"fault: CreateFileW");
            goto exit;
        }

        ntstatus = GetFileInfoInternal(hFile, &fileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            goto exit;
        }
    }

    ctx = new CreateContext(mStats, mCacheDataDir, argObjKey, fileInfo);
    APP_ASSERT(ctx);

    ctx->mLocalFile = hFile;
    hFile = INVALID_HANDLE_VALUE;

    // success

    *pFileInfo = fileInfo;

exit:
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

    return ctx;
}

CSDeviceContext* AwsS3::open(CALLER_ARG const ObjectKey& argObjKey,
    const UINT32 CreateOptions, const UINT32 GrantedAccess,
    const FSP_FSCTL_FILE_INFO& FileInfo)
{
    StatsIncr(open);

    NEW_LOG_BLOCK();

    // DoOpen() から呼び出されるが、ファイルを開く=ダウンロードになってしまうため
    // ここでは UParam に情報のみを保存し、DoRead() から呼び出される readFile() で
    // ファイルのダウンロード処理 (キャッシュ・ファイル) を行う。

    OpenContext* ctx = new OpenContext(mStats, mCacheDataDir, argObjKey, FileInfo, CreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    return ctx;
}

void AwsS3::close(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();

    OpenContext* op = dynamic_cast<OpenContext*>(argCSDeviceContext);
    if (op)
    {
    }

    CreateContext* cp = dynamic_cast<CreateContext*>(argCSDeviceContext);
    if (cp)
    {
    }

    delete argCSDeviceContext;
}

// EOF