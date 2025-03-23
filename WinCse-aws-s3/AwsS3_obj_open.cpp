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

    request.AddMetadata("wincse-creation-time", std::to_string(fileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-last-access-time", std::to_string(fileInfo.LastAccessTime).c_str());
    request.AddMetadata("wincse-last-write-time", std::to_string(fileInfo.LastWriteTime).c_str());

    request.AddMetadata("wincse-creation-time-debug", WinFileTime100nsToLocalTimeStringA(fileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-last-access-time-debug", WinFileTime100nsToLocalTimeStringA(fileInfo.LastAccessTime).c_str());
    request.AddMetadata("wincse-last-write-time-debug", WinFileTime100nsToLocalTimeStringA(fileInfo.LastWriteTime).c_str());

    const auto outcome = mClient.ptr->PutObject(request);

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

    const auto num = deleteCacheByObjKey(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d", num);

    if (pFileInfo)
    {
        *pFileInfo = fileInfo;
    }

    return true;
}

CSDeviceContext* AwsS3::create(CALLER_ARG const ObjectKey& argObjKey,
    const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 argFileAttributes,
    FSP_FSCTL_FILE_INFO* pFileInfo)
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

    // ファイル名への参照を登録

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        HANDLE hFile = INVALID_HANDLE_VALUE;

        if (CreateOptions & FILE_DIRECTORY_FILE)
        {
            // go next
        }
        else
        {
            const auto localPath{ mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(argObjKey.str()) };
            traceW(L"localPath=%s", localPath.c_str());

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
                GrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,//&SecurityAttributes,
                CREATE_ALWAYS, CreateFlags | FileAttributes, 0);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return nullptr;
            }
        }

        CreateContext* ctx = new CreateContext(mCacheDataDir, argObjKey, fileInfo, CreateOptions, GrantedAccess);
        APP_ASSERT(ctx);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            // ディレクトリの場合
        }
        else
        {
            // ファイルの場合

            ctx->mFile = hFile;
            APP_ASSERT(ctx->mFile.valid());

            // ファイル日時を同期

            if (!ctx->mFile.setFileTime(fileInfo.CreationTime, fileInfo.LastWriteTime))
            {
                APP_ASSERT(0);

                traceW(L"fault: setLocalTimeTime");
                delete ctx;

                return nullptr;
            }
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

void AwsS3::close(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();

    if (argCSDeviceContext)
    {
        CreateContext* ctx = dynamic_cast<CreateContext*>(argCSDeviceContext);
        if (ctx)
        {
            if (ctx->mFile.valid())
            {
                APP_ASSERT(ctx->mObjKey.meansFile());

                // エクスプローラーの新規作成から遷移したときの特別対応
                //
                // excel では作成時にデータを書き込み、直後に読み込まれるため
                // リモートの情報とローカルのキャッシュの状態を一致させる必要がある
                // 
                // 但し、コピー操作も同様にここを通過するため、サイズの制限を設け
                // 一定サイズ以内の場合は即時同期し、それ以外は遅延書き込みとする
                //
                // --> 数百MB のサイズのファイルがコピーされてきたときにアップロードに時間がかかってしまうことを懸念

                const auto fileSize = ctx->mFile.getFileSize();

                if (fileSize == 0)
                {
                    // nothing
                }
                else
                {
                    const auto remotePath{ ctx->getRemotePath() };

                    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // 名前への参照を登録
                    {
                        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

                        if (fileSize <= (FILESIZE_1B * 1024 * 1024))
                        {
                            // 1MB 以内なら即時アップロードする

                            ctx->mFile.close();

                            if (!putObject(CONT_CALLER ctx->mObjKey, ctx->getFilePathA().c_str(), nullptr))
                            {
                                traceW(L"fault: putObject");
                            }
                        }
                    }
                }

                traceW(L"close mObjKey=%s path=%s", ctx->mObjKey.c_str(), ctx->getFilePathW().c_str());
            }
        }
    }

    delete argCSDeviceContext;
}

    // EOF