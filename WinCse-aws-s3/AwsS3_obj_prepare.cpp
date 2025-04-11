#include "AwsS3.hpp"
#include "AwsS3_obj_pp_util.h"

using namespace WCSE;


static NTSTATUS syncFileAttributes(CALLER_ARG
    const FSP_FSCTL_FILE_INFO& fileInfo, const std::wstring& localPath, bool* pNeedDownload);

NTSTATUS AwsS3::prepareLocalFile_simple(CALLER_ARG OpenContext* ctx, UINT64 argOffset, ULONG argLength)
{
    NEW_LOG_BLOCK();

    const auto remotePath{ ctx->mObjKey.str() };

    traceW(L"remotePath=%s", remotePath.c_str());

    // ファイル名への参照を登録

    UnprotectedShare<PrepareLocalFileShare> unsafeShare(&mPrepareLocalFileShare, remotePath);    // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() 後の初回の呼び出し

            const auto localPath{ ctx->getCacheFilePath() };

            // ダウンロードが必要か判断

            bool needDownload = false;

            auto ntstatus = syncFileAttributes(CONT_CALLER ctx->mFileInfo, localPath, &needDownload);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: syncFileAttributes");
                return ntstatus;
            }

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

            if (!needDownload)
            {
                if (ctx->mFileInfo.FileSize == 0)
                {
                    // syncFileAttributes() でトランケート済

                    //return STATUS_END_OF_FILE;
                    return FspNtStatusFromWin32(ERROR_HANDLE_EOF);
                }
            }

            if (ctx->mFileInfo.FileSize <= PART_SIZE_BYTE)
            {
                // 一度で全てをダウンロード

                if (needDownload)
                {
                    // キャッシュ・ファイルの準備

                    const FileOutputParams outputParams{ localPath, CREATE_ALWAYS };

                    const auto bytesWritten = this->apicallGetObjectAndWriteToFile(CONT_CALLER ctx->mObjKey, outputParams);

                    if (bytesWritten < 0)
                    {
                        traceW(L"fault: apicallGetObjectAndWriteToFile bytesWritten=%lld", bytesWritten);

                        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
                    }
                }

                // 既存のファイルを開く

                ntstatus = ctx->openFileHandle(CONT_CALLER FILE_WRITE_ATTRIBUTES, OPEN_EXISTING);
                if (!NT_SUCCESS(ntstatus))
                {
                    traceW(L"fault: openFileHandle");
                    return ntstatus;
                }

                APP_ASSERT(ctx->mFile.valid());
            }
            else
            {
                // マルチパート・ダウンロード

                // ファイルを開く

                ntstatus = ctx->openFileHandle(CONT_CALLER
                    FILE_WRITE_ATTRIBUTES,
                    needDownload ? CREATE_ALWAYS : OPEN_EXISTING
                );

                if (!NT_SUCCESS(ntstatus))
                {
                    traceW(L"fault: openFileHandle");
                    return ntstatus;
                }

                APP_ASSERT(ctx->mFile.valid());

                if (needDownload)
                {
                    // ダウンロードが必要

                    if (!this->downloadMultipart(CONT_CALLER ctx, localPath))
                    {
                        traceW(L"fault: downloadMultipart");
                        //return STATUS_IO_DEVICE_ERROR;
                        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
                    }
                }
            }

            if (needDownload)
            {
                // ファイル日付の同期

                if (!ctx->mFile.setFileTime(ctx->mFileInfo))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setFileTime lerr=%lu", lerr);

                    return FspNtStatusFromWin32(lerr);
                }
            }
            else
            {
                // アクセス日時のみ更新

                if (!ctx->mFile.setFileTime(0, 0))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setFileTime lerr=%lu", lerr);

                    return FspNtStatusFromWin32(lerr);
                }
            }

            // 属性情報のサイズと比較

            LARGE_INTEGER fileSize;
            if(!::GetFileSizeEx(ctx->mFile.handle(), &fileSize))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

                return FspNtStatusFromWin32(lerr);
            }

            if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
            {
                APP_ASSERT(0);

                traceW(L"fault: no match filesize ");
                //return STATUS_IO_DEVICE_ERROR;
                return FspNtStatusFromWin32(ERROR_IO_DEVICE);
            }
        }
    }   // 名前のロックを解除 (safeShare の生存期間)

    APP_ASSERT(ctx->mFile.valid());

    return STATUS_SUCCESS;
}

static NTSTATUS syncFileAttributes(CALLER_ARG const FSP_FSCTL_FILE_INFO& remoteInfo,
    const std::wstring& localPath, bool* pNeedDownload)
{
    //
    // リモートのファイル属性をローカルのキャッシュ・ファイルに反映する
    // ダウンロードが必要な場合は pNeedDownload により通知
    //
    NEW_LOG_BLOCK();
    APP_ASSERT(pNeedDownload);

    //traceW(L"argObjKey=%s localPath=%s", argObjKey.c_str(), localPath.c_str());
    //traceW(L"remoteInfo FileSize=%llu LastWriteTime=%llu", remoteInfo.FileSize, remoteInfo.LastWriteTime);
    //traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", remoteInfo.CreationTime, remoteInfo.LastWriteTime);

    FSP_FSCTL_FILE_INFO localInfo{};

    // 
    // * パターン
    //      全て同じ場合は何もしない
    //      異なっているものがある場合は以下の表に従う
    // 
    //                                      +-----------------------------------------+
    //				                        | リモート                                |
    //                                      +---------------------+-------------------+
    //				                        | サイズ==0	          | サイズ>0          |
    // ------------+------------+-----------+---------------------+-------------------+
    //	ローカル   | 存在する   | サイズ==0 | 更新日時を同期      | ダウンロード      |
    //             |            +-----------+---------------------+-------------------+
    //			   |            | サイズ>0  | 切り詰め            | ダウンロード      |
    //             +------------+-----------+---------------------+-------------------+
    //		       | 存在しない	|	        | 空ファイル作成      | ダウンロード      |
    // ------------+------------+-----------+---------------------+-------------------+
    //
    bool syncTime = false;
    bool truncateFile = false;
    bool needDownload = false;
    DWORD lastError = ERROR_SUCCESS;
    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    FileHandle hFile = ::CreateFileW
    (
        localPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    lastError = ::GetLastError();

    if (hFile.valid())
    {
        //traceW(L"exists: local");

        // ローカル・ファイルが存在する

        ntstatus = GetFileInfoInternal(hFile.handle(), &localInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return ntstatus;
        }

        //traceW(L"localInfo FileSize=%llu LastWriteTime=%llu", localInfo.FileSize, localInfo.LastWriteTime);
        //traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", localInfo.CreationTime, localInfo.LastWriteTime);

        if (remoteInfo.FileSize == localInfo.FileSize &&
            localInfo.CreationTime == remoteInfo.CreationTime &&
            localInfo.LastWriteTime == remoteInfo.LastWriteTime)
        {
            // --> 全て同じなので処理不要

            traceW(L"same file, skip, localPath=%s", localPath.c_str());
        }
        else
        {
            if (remoteInfo.FileSize == 0)
            {
                if (localInfo.FileSize == 0)
                {
                    // ローカル == 0 : リモート == 0
                    // --> 更新日時を同期

                    syncTime = true;
                }
                else
                {
                    // ローカル > 0 : リモート == 0
                    // --> 切り詰め

                    truncateFile = true;
                }
            }
            else
            {
                // リモート > 0
                // --> ダウンロード

                needDownload = true;
            }
        }
    }
    else
    {
        if (lastError != ERROR_FILE_NOT_FOUND)
        {
            // 想定しないエラー

            traceW(L"fault: CreateFileW lerr=%lu", lastError);
            return FspNtStatusFromWin32(lastError);
        }

        //traceW(L"not exists: local");

        // ローカル・ファイルが存在しない

        if (remoteInfo.FileSize == 0)
        {
            // --> 空ファイル作成

            truncateFile = true;
        }
        else
        {
            // --> ダウンロード

            needDownload = true;
        }
    }

    traceW(L"syncRemoteTime=%s, truncateLocal=%s, needDownload=%s",
        BOOL_CSTRW(syncTime), BOOL_CSTRW(truncateFile), BOOL_CSTRW(needDownload));

    if (syncTime && truncateFile)
    {
        APP_ASSERT(0);
    }

    if (syncTime || truncateFile)
    {
        APP_ASSERT(!needDownload);

        hFile = ::CreateFileW
        (
            localPath.c_str(),
            FILE_WRITE_ATTRIBUTES, //GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            truncateFile ? CREATE_ALWAYS : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile.invalid())
        {
            lastError = ::GetLastError();
            traceW(L"fault: CreateFileW lerr=%lu", lastError);

            return FspNtStatusFromWin32(lastError);
        }

        // 更新日時を同期

        //traceW(L"setFileTime");

        if (!hFile.setFileTime(remoteInfo.CreationTime, remoteInfo.LastWriteTime))
        {
            lastError = ::GetLastError();
            traceW(L"fault: setFileTime lerr=%lu", lastError);

            return FspNtStatusFromWin32(lastError);
        }

        ntstatus = GetFileInfoInternal(hFile.handle(), &localInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return ntstatus;
        }

        if (truncateFile)
        {
            traceW(L"truncate localPath=%s", localPath.c_str());
        }
        else
        {
            traceW(L"sync localPath=%s", localPath.c_str());
        }
    }

    if (!needDownload)
    {
        // ダウンロードが不要な場合は、ローカルにファイルが存在する状態になっているはず

        APP_ASSERT(hFile.valid());
        APP_ASSERT(localInfo.CreationTime);
    }

    *pNeedDownload = needDownload;

    return STATUS_SUCCESS;
}

// EOF