#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


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

bool AwsS3::doMultipartDownload(CALLER_ARG WinCseLib::IOpenContext* argOpenContext, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);

    std::list<std::shared_ptr<FilePart>> fileParts;

    // 分割取得する領域を作成

    const int numParts = (int)((ctx->mFileInfo.FileSize + SIMPLE_DOWNLOAD_THRESHOLD - 1) / SIMPLE_DOWNLOAD_THRESHOLD);

    auto remaining = ctx->mFileInfo.FileSize;

    for (int i=0; i<numParts; i++)
    {
        fileParts.emplace_back
        (
            std::make_shared<FilePart>
            (
            mStats,
            SIMPLE_DOWNLOAD_THRESHOLD * i,
            (ULONG)min(SIMPLE_DOWNLOAD_THRESHOLD, remaining)
        )
        );

        remaining -= SIMPLE_DOWNLOAD_THRESHOLD;
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

        const auto reason = ::WaitForSingleObject(filePart->mDone, INFINITE);
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

            const auto reason = ::WaitForSingleObject(filePart->mDone, INFINITE);
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
NTSTATUS AwsS3::readObject_Multipart(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);
    APP_ASSERT(ctx->isFile());

    NEW_LOG_BLOCK();

    NTSTATUS ntstatus = STATUS_IO_DEVICE_ERROR;
    OVERLAPPED Overlapped{};

    const auto remotePath{ ctx->getRemotePath() };
    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx, ctx->mLocalFile, Offset, Length, remotePath.c_str());

    {
        // ファイル名への参照を登録

        UnprotectedNamedData<Shared_Simple> unsafeShare(remotePath);

        {
            // ファイル名のロック
            //
            // 複数スレッドから同一ファイルへの同時アクセスは行われない
            // --> ファイルを安全に操作できることを保証

            ProtectedNamedData<Shared_Simple> safeShare(unsafeShare);

            //
            // 関数先頭でも mLocalFile のチェックをしているが、ロック有無で状況が
            // 変わってくるため、改めてチェックする
            //
            if (ctx->mLocalFile == INVALID_HANDLE_VALUE)
            {
                traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                    ctx->mLocalFile, Offset, Length, remotePath.c_str());

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

                        if (!ctx->openLocalFile(GENERIC_WRITE, CREATE_ALWAYS))
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

                    ntstatus = STATUS_END_OF_FILE;
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

                APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

                traceW(L"needDownload: %s", needDownload ? L"true" : L"false");

                if (needDownload)
                {
                    // ダウンロードが必要

                    if (!doMultipartDownload(CONT_CALLER ctx, localPath))
                    {
                        traceW(L"fault: doMultipartDownload");
                        goto exit;
                    }
                }

                APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

                // 属性情報のサイズと比較

                LARGE_INTEGER fileSize;
                if(!::GetFileSizeEx(ctx->mLocalFile, &fileSize))
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

            // ファイル名のロックを解放
        }

        // ファイル名への参照を解除
    }

    APP_ASSERT(ctx->mLocalFile);
    APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

    // Offset, Length によりファイルを読む

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mLocalFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mLocalFile, Offset, Length, *PBytesTransferred);

    ntstatus = STATUS_SUCCESS;

exit:
    traceW(L"ntstatus=%ld", ntstatus);

    return ntstatus;
}

// EOF