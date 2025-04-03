#include "AwsS3.hpp"

using namespace WinCseLib;


struct FilePart
{
    WINCSE_DEVICE_STATS* mStats;
    const UINT64 mOffset;
    const ULONG mLength;

    EventHandle mDone;
    bool mResult = false;

    std::atomic<bool> mInterrupt = false;

    FilePart(WINCSE_DEVICE_STATS* argStats, UINT64 argOffset, ULONG argLength)
        : mStats(argStats), mOffset(argOffset), mLength(argLength)
    {
        mDone = ::CreateEventW(NULL,
            TRUE,				// 手動リセットイベント
            FALSE,				// 初期状態：非シグナル状態
            NULL);

        APP_ASSERT(mDone.valid());
    }

    void SetResult(bool argResult)
    {
        mResult = argResult;
        const auto b = ::SetEvent(mDone.handle());					// シグナル状態に設定
        APP_ASSERT(b);
    }

    ~FilePart()
    {
        mDone.close();
    }
};

struct ReadPartTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::No; }
    Priority getPriority() const noexcept override { return Priority::Middle; }

    AwsS3* mAwsS3;
    const ObjectKey mObjKey;
    const std::wstring mLocalPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(AwsS3* argAwsS3, const ObjectKey& argObjKey,
        const std::wstring argLocalPath, std::shared_ptr<FilePart> argFilePart)
        : mAwsS3(argAwsS3), mObjKey(argObjKey), mLocalPath(argLocalPath), mFilePart(argFilePart)
    {
    }

    void run(CALLER_ARG0)
    {
        NEW_LOG_BLOCK();

        bool result = false;

        try
        {
            if (mFilePart->mInterrupt)
            {
                traceW(L"Interruption request received");
            }
            else
            {
                const FileOutputParams outputParams
                {
                    mLocalPath,
                    OPEN_EXISTING,
                    true,                   // SetRange()
                    mFilePart->mOffset,
                    mFilePart->mLength
                };

                const auto bytesWritten = mAwsS3->getObjectAndWriteToFile(CONT_CALLER mObjKey, outputParams);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: getObjectAndWriteToFile_Multipart bytesWritten=%lld", bytesWritten);
                }
            }
        }
        catch (const std::exception& ex)
        {
            traceA("catch exception: what=[%s]", ex.what());
        }
        catch (...)
        {
            traceW(L"catch unknown");
        }

        // 結果を設定し、シグナル状態に変更
        // --> WaitForSingleObject で待機しているスレッドのロックが解除される

        mFilePart->SetResult(result);
    }
};

bool AwsS3::doMultipartDownload(CALLER_ARG OpenContext* ctx, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    // 一つのパート・サイズ

    std::list<std::shared_ptr<FilePart>> fileParts;

    // 分割取得する領域を作成

    const int numParts = (int)((ctx->mFileInfo.FileSize + PART_LENGTH_BYTE - 1) / PART_LENGTH_BYTE);

    auto remaining = ctx->mFileInfo.FileSize;

    for (int i=0; i<numParts; i++)
    {
        fileParts.emplace_back
        (
            std::make_shared<FilePart>
            (
                mStats,
                PART_LENGTH_BYTE * i,                       // Offset
                (ULONG)min(PART_LENGTH_BYTE, remaining)     // Length
            )
        );

        remaining -= PART_LENGTH_BYTE;
    }

    for (auto& filePart: fileParts)
    {
        // マルチパートの読み込みを遅延タスクに登録

        getWorker(L"delayed")->addTask(CONT_CALLER new ReadPartTask(this, ctx->mObjKey, localPath, filePart));
    }

    bool errorExists = false;

    for (auto& filePart: fileParts)
    {
        // タスクの完了を待機

        const auto reason = ::WaitForSingleObject(filePart->mDone.handle(), INFINITE);
        APP_ASSERT(reason == WAIT_OBJECT_0);

        if (!filePart->mResult)
        {
            // エラーがあるパートを発見

            errorExists = true;
            break;
        }
    }

    if (errorExists)
    {
        // マルチパートの一部にエラーが存在したので、全ての遅延タスクを中断して終了

        for (auto& filePart: fileParts)
        {
            // 全てのパートに中断フラグを立てる
            filePart->mInterrupt = true;
        }

        for (auto& filePart: fileParts)
        {
            // タスクの完了を待機

            const auto reason = ::WaitForSingleObject(filePart->mDone.handle(), INFINITE);
            APP_ASSERT(reason == WAIT_OBJECT_0);

            if (!filePart->mResult)
            {
                traceW(L"error offset=%lld", filePart->mOffset);
            }
        }

        traceW(L"error exists");
        return false;
    }

    return true;
}

NTSTATUS AwsS3::prepareLocalFile_Multipart(CALLER_ARG OpenContext* ctx)
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

            std::wstring localPath;

            if (!ctx->getCacheFilePath(&localPath))
            {
                //traceW(L"fault: getCacheFilePath");
                //return STATUS_OBJECT_NAME_NOT_FOUND;
                return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
            }

            // ダウンロードが必要か判断

            bool needDownload = false;

            NTSTATUS ntstatus = syncFileAttributes(CONT_CALLER ctx->mFileInfo, localPath, &needDownload);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: syncFileAttributes");
                return ntstatus;
            }

            //traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

            if (!needDownload)
            {
                if (ctx->mFileInfo.FileSize == 0)
                {
                    // syncFileAttributes() でトランケート済

                    //return STATUS_END_OF_FILE;
                    return FspNtStatusFromWin32(ERROR_HANDLE_EOF);
                }
            }

            ntstatus = ctx->openFileHandle(CONT_CALLER
                //needDownload ? FILE_WRITE_ATTRIBUTES : 0,
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

                if (!this->doMultipartDownload(CONT_CALLER ctx, localPath))
                {
                    traceW(L"fault: doMultipartDownload");
                    //return STATUS_IO_DEVICE_ERROR;
                    return FspNtStatusFromWin32(ERROR_IO_DEVICE);
                }

                // ファイル日付の同期

                if (!ctx->mFile.setFileTime(ctx->mFileInfo))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setBasicInfo lerr=%lu", lerr);

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

// EOF