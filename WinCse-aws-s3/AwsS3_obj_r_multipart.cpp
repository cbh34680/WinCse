#include "AwsS3.hpp"
#include <filesystem>


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

struct ReadPartTask : public ITask
{
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

                const auto bytesWritten = mAwsS3->prepareLocalCacheFile(CONT_CALLER mObjKey, outputParams);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: prepareLocalCacheFile_Multipart bytesWritten=%lld", bytesWritten);
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

bool AwsS3::doMultipartDownload(CALLER_ARG WinCseLib::CSDeviceContext* ctx, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    // 一つのパート・サイズ
    const auto PART_LENGTH_BYTE = FILESIZE_1BU * 1024U * 1024 * 4;

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

        ITask* task = new ReadPartTask(this, ctx->mObjKey, localPath, filePart);
        APP_ASSERT(task);

        mDelayedWorker->addTask(CONT_CALLER task, Priority::Middle, CanIgnoreDuplicates::No);
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

//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
NTSTATUS AwsS3::readObject_Multipart(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
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

            const auto localPath{ ctx->getFilePathW() };

            // ダウンロードが必要か判断

            bool needDownload = false;

            if (!syncFileAttributes(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
            {
                traceW(L"fault: syncFileAttributes");
                return STATUS_IO_DEVICE_ERROR;
            }

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

            DWORD dwCreationDisposition = 0;

            if (needDownload)
            {
                // ダウンロードするので、新規作成か切り詰めてファイルを開く

                dwCreationDisposition = CREATE_ALWAYS;
            }
            else
            {
                // ダウンロードが不要な場合はローカル・キャッシュを参考できる状態になっているはず

                dwCreationDisposition = OPEN_EXISTING;

                if (ctx->mFileInfo.FileSize == 0)
                {
                    return STATUS_END_OF_FILE;
                }
            }

            // ファイルを開く

            DWORD dwDesiredAccess = ctx->mGrantedAccess;
            if (needDownload)
            {
                dwDesiredAccess |= FILE_WRITE_ATTRIBUTES;           // SetFileTime() に必要
            }

            ULONG CreateFlags = 0;
            //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // ディレクトリは操作しない

            if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

            ctx->mFile = ::CreateFileW(ctx->getFilePathW().c_str(),
                dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
                dwCreationDisposition, CreateFlags, 0);

            if (ctx->mFile.invalid())
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return FspNtStatusFromWin32(lerr);
            }

            if (needDownload)
            {
                // ダウンロードが必要

                if (!this->doMultipartDownload(CONT_CALLER ctx, localPath))
                {
                    traceW(L"fault: doMultipartDownload");
                    return STATUS_IO_DEVICE_ERROR;
                }

                // ファイル日時を同期

                if (!ctx->mFile.setFileTime(ctx->mFileInfo.CreationTime, ctx->mFileInfo.LastWriteTime))
                {
                    traceW(L"fault: setLocalTimeTime");
                    return STATUS_IO_DEVICE_ERROR;
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
                return STATUS_IO_DEVICE_ERROR;
            }
        }
    }   // 名前のロックを解除 (safeShare の生存期間)

    APP_ASSERT(ctx->mFile.valid());

    // Offset, Length によりファイルを読む

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: ReadFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mFile.handle(), Offset, Length, *PBytesTransferred);

    return STATUS_SUCCESS;
}

// EOF