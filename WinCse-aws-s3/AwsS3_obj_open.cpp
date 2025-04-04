#include "AwsS3.hpp"
#include <fstream>
#include <iostream>

using namespace WCSE;


CSDeviceContext* AwsS3::create(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& fileInfo, const UINT32 CreateOptions,
    const UINT32 GrantedAccess, const UINT32 argFileAttributes)
{
    StatsIncr(create);
    NEW_LOG_BLOCK();

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    const auto remotePath{ argObjKey.str() };

    UnprotectedShare<PrepareLocalFileShare> unsafeShare(&mPrepareLocalFileShare, remotePath);   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        FileHandle hFile;

        if (CreateOptions & FILE_DIRECTORY_FILE)
        {
            // go next
        }
        else
        {
            std::wstring localPath;

            if (!GetCacheFilePath(mCacheDataDir, argObjKey.str(), &localPath))
            {
                traceW(L"fault: GetCacheFilePath");
                return nullptr;
            }

            //traceW(L"localPath=%s", localPath.c_str());

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
                NULL,
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

            if (!hFile.setFileTime(fileInfo))
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
    // ファイルのダウンロード処理 (キャッシュ・ファイルの作成) を行う。

    OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, FileInfo, CreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    return ctx;
}

void AwsS3::close(CALLER_ARG WCSE::CSDeviceContext* ctx)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    //traceW(L"close mObjKey=%s", ctx->mObjKey.c_str());

    if (ctx->mFile.valid() && ctx->mFlags & CSDCTX_FLAGS_MODIFY)
    {
        // DoCleanup() で削除されるファイルはクローズされているので
        // ここを通過するのはアップロードする必要のあるファイルのみとなっているはず

        APP_ASSERT(ctx->isFile());
        APP_ASSERT(ctx->mObjKey.meansFile());

        // キャッシュ・ファイル名

        std::wstring localPath;
        if (!ctx->getCacheFilePath(&localPath))
        {
            traceW(L"fault: getCacheFilePath");
            return;
        }

        // 閉じる前に属性情報を取得

        if (!::FlushFileBuffers(ctx->mFile.handle()))
        {
            APP_ASSERT(0);

            const auto lerr = ::GetLastError();

            traceW(L"fault: FlushFileBuffers, lerr=%lu", lerr);
            return;
        }

        FSP_FSCTL_FILE_INFO fileInfo;
        NTSTATUS ntstatus = GetFileInfoInternal(ctx->mFile.handle(), &fileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return;
        }

        // 閉じておかないと putObject() にある Aws::FStream が失敗する

        ctx->mFile.close();

        const auto remotePath{ ctx->mObjKey.str() };

        if (fileInfo.FileSize < FILESIZE_1GiB * 5)
        {
            if (!putObject(CONT_CALLER ctx->mObjKey, fileInfo, localPath.c_str()))
            {
                traceW(L"fault: putObject");
                return;
            }
        }
        else
        {
            traceW(L"fault: too big");
            return;
        }

        // putObject でメモリ・キャッシュが削除されているので、改めて取得
        // --> 必須ではないが、作成直後に属性が参照されることに対応

        if (!headObject_File(CONT_CALLER ctx->mObjKey, nullptr))
        {
            traceW(L"fault: headObject_File");
            return;
        }

        if (mConfig.deleteAfterUpload)
        {
            traceW(L"delete local cache: %s", localPath.c_str());

            if (!::DeleteFile(localPath.c_str()))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: DeleteFile lerr=%lu", lerr);

                return;
            }

            //traceW(L"success");
        }
    }
}

    // EOF