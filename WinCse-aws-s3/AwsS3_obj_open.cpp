#include "AwsS3.hpp"
#include <fstream>
#include <iostream>

using namespace WinCseLib;


bool AwsS3::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const char* sourceFile, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    FSP_FSCTL_FILE_INFO fileInfo{};

    if (sourceFile == nullptr)
    {
        // create() から呼び出される場合はこちらを通過
        // --> まだローカル・キャッシュも作成される前なので、ファイル名もない

        FILETIME ft;
        ::GetSystemTimeAsFileTime(&ft);

        const auto dirInfo{ makeDirInfo_byName(argObjKey, WinFileTimeToWinFileTime100ns(ft)) };

        fileInfo = dirInfo->FileInfo;
    }
    else
    {
        // ローカル・キャッシュの内容をアップロードする

        APP_ASSERT(argObjKey.meansFile());

        if (!PathToFileInfoA(sourceFile, &fileInfo))
        {
            traceW(L"fault: PathToFileInfoA");
            return false;
        }

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>
        (
            __FUNCTION__,
            sourceFile,
            std::ios_base::in | std::ios_base::binary
        );

        if (!inputData->good())
        {
            traceW(L"fault: inputData->good");
            return false;
        }

        request.SetBody(inputData);
    }

    request.AddMetadata("wincse-file-attributes", std::to_string(fileInfo.FileAttributes).c_str());
    request.AddMetadata("wincse-creation-time", std::to_string(fileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-last-access-time", std::to_string(fileInfo.LastAccessTime).c_str());
    request.AddMetadata("wincse-last-write-time", std::to_string(fileInfo.LastWriteTime).c_str());

#if _DEBUG
    request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(fileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-debug-last-access-time", WinFileTime100nsToLocalTimeStringA(fileInfo.LastAccessTime).c_str());
    request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(fileInfo.LastWriteTime).c_str());
#endif

    const auto outcome = mClient->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    // キャッシュ・メモリから削除
    //
    // 後続の処理で DoGetSecurityByName() が呼ばれるが、上記で作成したディレクトリが
    // キャッシュに反映されていない状態で利用されてしまうことを回避するために
    // 事前に削除しておき、改めてキャッシュを作成させる

    const auto num = deleteCacheByObjectKey(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d", num);

    if (pFileInfo)
    {
        *pFileInfo = fileInfo;
    }

    return true;
}

CSDeviceContext* AwsS3::create(CALLER_ARG const ObjectKey& argObjKey,
    const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 argFileAttributes,
    PSECURITY_DESCRIPTOR SecurityDescriptor, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    StatsIncr(create);
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.hasKey());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    FSP_FSCTL_FILE_INFO fileInfo{};

    ObjectKey objKey{ CreateOptions & FILE_DIRECTORY_FILE ? std::move(argObjKey.toDir()) : argObjKey };

    if (headObject(CONT_CALLER objKey, &fileInfo))
    {
        // 存在しているので、fileInfo はそのまま利用できる

        // --> ディレクトリの場合は存在有無に関わらず、上位の階層から下位に向かって
        //     何度も呼び出されるので、存在するときは何もしない
    }
    else
    {
        // 存在しなければ作成

        // リモートに空ファイルやディレクトリを作成し、その情報が fileInfo に保存される

        if (!putObject(CONT_CALLER objKey, nullptr, &fileInfo))
        {
            traceW(L"fault: putObject");
            return nullptr;
        }
    }

    traceW(L"objKey=%s", objKey.c_str());

    const auto remotePath{ objKey.str() };

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        FileHandle hFile;

        if (CreateOptions & FILE_DIRECTORY_FILE)
        {
            // go next
        }
        else
        {
            std::wstring encPath;
            if (!EncodeFileNameToLocalNameW(argObjKey.str(), &encPath))
            {
                traceW(L"fault: EncodeFileNameToLocalNameW");
                return nullptr;
            }

            const auto localPath{ mCacheDataDir + L'\\' + encPath };
            traceW(L"localPath=%s", localPath.c_str());

#if 0
            SECURITY_ATTRIBUTES SecurityAttributes{};
            SecurityAttributes.nLength = sizeof SecurityAttributes;
            SecurityAttributes.lpSecurityDescriptor = SecurityDescriptor;
            SecurityAttributes.bInheritHandle = FALSE;
#endif

            UINT32 FileAttributes = argFileAttributes;
            ULONG CreateFlags = 0;
            //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // ディレクトリは操作しないので不要

            if (CreateOptions & FILE_DELETE_ON_CLOSE)
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

            //if (CreateOptions & FILE_DIRECTORY_FILE)
            //{
                /*
                * It is not widely known but CreateFileW can be used to create directories!
                * It requires the specification of both FILE_FLAG_BACKUP_SEMANTICS and
                * FILE_FLAG_POSIX_SEMANTICS. It also requires that FileAttributes has
                * FILE_ATTRIBUTE_DIRECTORY set.
                */
                //CreateFlags |= FILE_FLAG_POSIX_SEMANTICS;         // ディレクトリは操作しないので不要
                //FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;       // ディレクトリは操作しないので不要
            //}
            //else
                FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

            if (0 == FileAttributes)
                FileAttributes = FILE_ATTRIBUTE_NORMAL;

            hFile = ::CreateFileW(localPath.c_str(),
                GrantedAccess,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL, //&SecurityAttributes,
                CREATE_ALWAYS,
                CreateFlags | FileAttributes,
                NULL);

            if (hFile.invalid())
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return nullptr;
            }

            // ファイル日時を同期

            if (!hFile.setFileTime(fileInfo.CreationTime, fileInfo.LastWriteTime))
            {
                traceW(L"fault: setLocalTimeTime");

                return nullptr;
            }
        }

        OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, fileInfo, CreateOptions, GrantedAccess);
        APP_ASSERT(ctx);

        if (hFile.valid())
        {
            // ファイルの場合

            ctx->mFile = std::move(hFile);

            APP_ASSERT(ctx->mFile.valid());
            APP_ASSERT(hFile.invalid());
        }

        *pFileInfo = fileInfo;

        return ctx;

    }   // 名前のロックを解除 (safeShare の生存期間)
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

    OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, FileInfo, CreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    return ctx;
}

void AwsS3::cleanup(CALLER_ARG WinCseLib::CSDeviceContext* ctx, ULONG Flags)
{
    StatsIncr(cleanup);
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    if (Flags & FspCleanupDelete)
    {
        // setDelete() により削除フラグを設定されたファイルと、
        // CreateFile() 時に FILE_FLAG_DELETE_ON_CLOSE の属性が与えられたファイル
        // がクローズされるときにここを通過する

        Aws::S3::Model::DeleteObjectRequest request;
        request.SetBucket(ctx->mObjKey.bucketA());
        request.SetKey(ctx->mObjKey.keyA());
        const auto outcome = mClient->DeleteObject(request);

        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: DeleteObject");
        }

        // キャッシュ・メモリから削除

        const auto num = deleteCacheByObjectKey(CONT_CALLER ctx->mObjKey);
        traceW(L"cache delete num=%d", num);

        // WinFsp の Cleanup() で CloseHandle() しているので、同様の処理を行う

        ctx->mFile.close();
    }
}

void AwsS3::close(CALLER_ARG WinCseLib::CSDeviceContext* ctx)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    traceW(L"close mObjKey=%s", ctx->mObjKey.c_str());

    if (ctx->mFile.valid() && ctx->mFlags & CSDCTX_FLAGS_WRITE)
    {
        // cleanup() で削除されるファイルはクローズされているので
        // ここを通過するのはアップロードする必要のあるファイルのみとなっているはず

        APP_ASSERT(ctx->mObjKey.meansFile());

        const auto fileSize = ctx->mFile.getFileSize();

        traceW(L"fileSize=%lld", fileSize);

        if (fileSize == 0)
        {
            // nothing
        }
        else
        {
            // 閉じておかないと putObject() にある Aws::FStream が失敗する

            ctx->mFile.close();

            const auto remotePath{ ctx->getRemotePath() };

            UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // 名前への参照を登録
            {
                const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

                if (fileSize < FILESIZE_1GiB * 5)
                {
                    std::string localPath;

                    if (ctx->getFilePathA(&localPath))
                    {
                        if (!putObject(CONT_CALLER ctx->mObjKey, localPath.c_str(), nullptr))
                        {
                            traceW(L"fault: putObject");
                        }
                    }
                    else
                    {
                        traceW(L"fault: getFilePathA");
                    }
                }
                else
                {
                    traceW(L"fault: too big");
                }

            }   // 名前のロックを解除 (safeShare の生存期間)
        }
    }

    delete ctx;
}

    // EOF