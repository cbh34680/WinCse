#include "CSDevice.hpp"

using namespace WCSE;




CSDeviceContext* CSDevice::create(CALLER_ARG const ObjectKey& argObjKey,
    UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s", argObjKey.c_str());

    UINT32 GrantedAccess = argGrantedAccess;
    UINT32 FileAttributes = argFileAttributes;
    const bool isDirectory = argCreateOptions & FILE_DIRECTORY_FILE;

    FileHandle hFile;
    HANDLE Handle = INVALID_HANDLE_VALUE;

    const auto localPath{ GetCacheFilePath(mRuntimeEnv->CacheDataDir, argObjKey.str()) };

    traceW(L"localPath=%s", localPath.c_str());

    ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;

    if (argCreateOptions & FILE_DELETE_ON_CLOSE)
    {
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
    }

    if (isDirectory)
    {
        Handle = mRefDir.handle();
    }
    else
    {
        FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

        if (0 == FileAttributes)
        {
            FileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

        hFile = ::CreateFileW(localPath.c_str(),
            GrantedAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            isDirectory ? CREATE_NEW : CREATE_ALWAYS,
            CreateFlags | FileAttributes,
            NULL);

        if (hFile.invalid())
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: CreateFileW lerr=%lu", lerr);

            return nullptr;
        }

        Handle = hFile.handle();
    }

    APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

    FSP_FSCTL_FILE_INFO fileInfo;

    const auto ntstatus = GetFileInfoInternal(Handle, &fileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return nullptr;
    }

    Handle = INVALID_HANDLE_VALUE;      // これ以降は使わない

    APP_ASSERT(fileInfo.LastWriteTime);

    auto ctx = std::make_unique<OpenContext>(mRuntimeEnv->CacheDataDir, argObjKey, fileInfo, argCreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    if (isDirectory)
    {
#if XCOPY_DIR
        // ここで作成してしまう

        if (!this->putObject(CONT_CALLER argObjKey, fileInfo, nullptr))
        {
            traceW(L"fault: putObject");
            return nullptr;
        }
#endif
        // ディレクトリの場合は hFile は保存せず、このまま閉じる
    }
    else
    {
        // ファイルの場合

        ctx->mFile = std::move(hFile);

        // move の確認

        APP_ASSERT(ctx->mFile.valid());
        APP_ASSERT(hFile.invalid());
    }

    // キャッシュの削除
    // 
    // --> 親ディレクトリのキャッシュを削除しておかないと、新規作成したものが
    //     反映されない状態になってしまう

    const auto num = mQueryObject->deleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    OpenContext* ret = ctx.get();
    ctx.release();

    return ret;
}

CSDeviceContext* CSDevice::open(CALLER_ARG const ObjectKey& argObjKey,
    UINT32 CreateOptions, UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo)
{
    NEW_LOG_BLOCK();

    // DoOpen() から呼び出されるが、ファイルを開く=ダウンロードになってしまうため
    // ここでは UParam に情報のみを保存し、DoRead() から呼び出される readFile() で
    // ファイルのダウンロード処理 (キャッシュ・ファイルの作成) を行う。

    OpenContext* ctx = new OpenContext(mRuntimeEnv->CacheDataDir, argObjKey, FileInfo, CreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    return ctx;
}

bool CSDevice::uploadWhenClosing(CALLER_ARG WCSE::CSDeviceContext* ctx, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();

    FSP_FSCTL_FILE_INFO fileInfo{};

    if (ctx->isDir())
    {
        // create の情報を転記

        fileInfo = ctx->mFileInfo;
    }
    else if (ctx->isFile())
    {
        APP_ASSERT(ctx->mFile.valid());
        APP_ASSERT(ctx->mObjKey.meansFile());

        // ファイルの場合は Write の結果を反映するため Flush

        traceW(L"valid file");

        // 属性情報を取得するため、ファイルを閉じる前に flush する

        if (!::FlushFileBuffers(ctx->mFile.handle()))
        {
            const auto lerr = ::GetLastError();

            traceW(L"fault: FlushFileBuffers, lerr=%lu", lerr);
            return false;
        }

        const auto ntstatus = GetFileInfoInternal(ctx->mFile.handle(), &fileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return false;
        }

        // 閉じておかないと putObject() にある Aws::FStream が失敗する

        ctx->mFile.close();
    }

    // アップロード

    traceW(L"putObject mObjKey=%s, argSourcePath=%s", ctx->mObjKey.c_str(), argSourcePath);

#if XCOPY_V
    if (!this->putObjectViaListLock(CONT_CALLER ctx->mObjKey, fileInfo, argSourcePath))
#else
    if (!this->putObject(CONT_CALLER ctx->mObjKey, fileInfo, argSourcePath))
#endif
    {
        traceW(L"fault: putObject");
        return false;
    }

    return true;
}

static std::wstring CsdCtxFlagsStr(uint32_t flags)
{
    std::list<std::wstring> strs;

    if (flags & CSDCTX_FLAGS_MODIFY)            strs.emplace_back(L"modify");
    if (flags & CSDCTX_FLAGS_READ)              strs.emplace_back(L"read");
    if (flags & CSDCTX_FLAGS_M_CREATE)          strs.emplace_back(L"create");
    if (flags & CSDCTX_FLAGS_M_WRITE)           strs.emplace_back(L"write");
    if (flags & CSDCTX_FLAGS_M_OVERWRITE)       strs.emplace_back(L"overwrite");
    if (flags & CSDCTX_FLAGS_M_SET_BASIC_INFO)  strs.emplace_back(L"set_basic_info");
    if (flags & CSDCTX_FLAGS_M_SET_FILE_SIZE)   strs.emplace_back(L"set_file_size");

    return JoinStrings(strs, L", ", false);
}

void CSDevice::close(CALLER_ARG WCSE::CSDeviceContext* ctx)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    //traceW(L"close mObjKey=%s", ctx->mObjKey.c_str());

    if (!(ctx->mFlags & CSDCTX_FLAGS_MODIFY))
    {
        return;
    }

#if XCOPY_DIR
    if (ctx->isDir())
    {
        return;
    }
#endif

    traceW(L"mFlage=%s", CsdCtxFlagsStr(ctx->mFlags).c_str());

    // キャッシュ・ファイル名

    const auto localPath{ ctx->getCacheFilePath() };

    // ディレクトリの場合は空コンテンツ

    PCWSTR sourcePath{ ctx->isDir() ? nullptr : localPath.c_str() };

    // ファイル・アップロード

    traceW(L"uploadWhenClosing mObjKey=%s, sourcePath=%s", ctx->mObjKey.c_str(), sourcePath);

    if (this->uploadWhenClosing(CONT_CALLER ctx, sourcePath))
    {
        traceW(L"success: uploadWhenClosing");
    }
    else
    {
        traceW(L"fault: uploadWhenClosing");

        // 後続処理があるので return はしない
        // return
    }

    if (sourcePath)
    {
        // (config の設定により)アップロードしたファイルを削除する

        if (mRuntimeEnv->DeleteAfterUpload)
        {
            // ファイルを削除

            traceW(L"delete local cache: %s", sourcePath);

            if (!::DeleteFileW(sourcePath))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: DeleteFileW, lerr=%lu", lerr);
            }

            //traceW(L"success");
        }
    }
}

    // EOF