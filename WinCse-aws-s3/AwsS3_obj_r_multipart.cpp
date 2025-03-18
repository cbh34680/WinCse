#include "AwsS3.hpp"
#include <filesystem>


// 一つのパート・サイズ
#define PART_LENGTH_BYTE		(1024ULL * 1024 * 4)


using namespace WinCseLib;


struct FilePart
{
    WINCSE_DEVICE_STATS* mStats;
    const UINT64 mOffset;
    const ULONG mLength;

    EventHandleRAII mDone;
    bool mResult = false;

    std::atomic<bool> mInterrupt = false;

    FilePart(WINCSE_DEVICE_STATS* argStats, UINT64 argOffset, ULONG argLength)
        : mStats(argStats), mOffset(argOffset), mLength(argLength)
    {
        StatsIncr(_CreateEvent);

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
        StatsIncr(_CloseHandle_Event);
        mDone.close();
    }
};

struct ReadPartTask : public ITask
{
    AwsS3* mS3;
    const ObjectKey mObjKey;
    const std::wstring mLocalPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(AwsS3* that, const ObjectKey& argObjKey,
        const std::wstring argLocalPath, std::shared_ptr<FilePart> argFilePart)
        : mS3(that), mObjKey(argObjKey), mLocalPath(argLocalPath), mFilePart(argFilePart)
    {
    }

    void run(CALLER_ARG0)
    {
        NEW_LOG_BLOCK();

        traceW(L"ReadPartTask::run");

        bool result = false;

        try
        {
            if (mFilePart->mInterrupt)
            {
                traceW(L"Interruption request received");
            }
            else
            {
                const FileOutputMeta meta
                {
                    mLocalPath,
                    OPEN_EXISTING,
                    true,                   // SetRange()
                    mFilePart->mOffset,
                    mFilePart->mLength,
                    false                   // SetFileTime()
                };

                const auto bytesWritten = mS3->prepareLocalCacheFile(CONT_CALLER mObjKey, meta);

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

bool AwsS3::doMultipartDownload(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();

    CSDeviceContext* ctx = dynamic_cast<CSDeviceContext*>(argCSDeviceContext);

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
        // マルチパートの一部にエラーが存在したので、全ての遅延タスクを
        // キャンセルして終了

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

    // タイムスタンプを更新

    if (!ctx->setLocalFileTime(ctx->mFileInfo.CreationTime))
    {
        traceW(L"fault: setLocalTimeTime");
        return false;
    }

    return true;
}

//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
struct Shared : public SharedBase { };
static ShareStore<Shared> gSharedStore;

bool AwsS3::readObject_Multipart(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx->isFile());

    NEW_LOG_BLOCK();

    bool ret = false;
    OVERLAPPED Overlapped{};

    const auto remotePath{ ctx->getRemotePath() };
    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx, ctx->mLocalFile.handle(), Offset, Length, remotePath.c_str());

    // ファイル名への参照を登録

    UnprotectedShare<Shared> unsafeShare(&gSharedStore, remotePath);                // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        //
        // 関数先頭でも mLocalFile のチェックをしているが、ロック有無で状況が
        // 変わってくるため、改めてチェックする
        //
        if (ctx->mLocalFile.invalid())
        {
            traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                ctx->mLocalFile.handle(), Offset, Length, remotePath.c_str());

            // openFile() 後の初回の呼び出し

            const std::wstring localPath{ ctx->getLocalPath() };

            if (ctx->mFileInfo.FileSize == 0)
            {
                // ファイルが空なのでダウンロードは不要

                const auto alreadyExists = std::filesystem::exists(localPath);

                if (!alreadyExists)
                {
                    // ローカルに存在しないので touch と同義

                    // タイムスタンプを属性情報に合わせる
                    // SetFileTime を実行するので、GENERIC_WRITE が必要

                    if (!ctx->openLocalFile(FILE_WRITE_ATTRIBUTES, CREATE_ALWAYS))
                    {
                        traceW(L"fault: openFile");
                        goto exit;
                    }

                    if (!ctx->setLocalFileTime(ctx->mFileInfo.CreationTime))
                    {
                        traceW(L"fault: setLocalTimeTime");
                        goto exit;
                    }
                }

                // ファイルが空なので、EOF を返却

                ret = true;
                goto exit;
            }

            // ダウンロードが必要か判断

            bool needDownload = false;

            if (!shouldDownload(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
            {
                traceW(L"fault: shouldDownload");
                goto exit;
            }

            if (!ctx->openLocalFile(GENERIC_WRITE, needDownload ? CREATE_ALWAYS : OPEN_EXISTING))
            {
                traceW(L"fault: openFile");
                goto exit;
            }

            APP_ASSERT(ctx->mLocalFile.valid());

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

            if (needDownload)
            {
                // ダウンロードが必要

                if (!doMultipartDownload(CONT_CALLER ctx, localPath))
                {
                    traceW(L"fault: doMultipartDownload");
                    goto exit;
                }
            }

            APP_ASSERT(ctx->mLocalFile.valid());

            // 属性情報のサイズと比較

            LARGE_INTEGER fileSize;
            if(!::GetFileSizeEx(ctx->mLocalFile.handle(), &fileSize))
            {
                traceW(L"fault: GetFileSizeEx");
                goto exit;
            }

            if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
            {
                traceW(L"fault: no match filesize ");
                goto exit;
            }
        }
    }   // 名前のロックを解除 (safeShare の生存期間)

    APP_ASSERT(ctx->mLocalFile.valid());

    // Offset, Length によりファイルを読む

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mLocalFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mLocalFile.handle(), Offset, Length, *PBytesTransferred);

    ret = true;

exit:
    traceW(L"ret=%s", BOOL_CSTRW(ret));

    return ret;
}

// EOF