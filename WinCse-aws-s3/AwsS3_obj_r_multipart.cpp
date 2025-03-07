#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


struct ReadPartTask : public ITask
{
    AwsS3* mS3;
    const std::wstring mBucket;
    const std::wstring mKey;
    const std::wstring mLocalPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(AwsS3* that, const std::wstring& argBucket, const std::wstring& argKey,
        const std::wstring argLocalPath, std::shared_ptr<FilePart> argFilePart)
        : mS3(that), mBucket(argBucket), mKey(argKey), mLocalPath(argLocalPath), mFilePart(argFilePart)
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
                const FileOutputMeta meta{ mLocalPath, OPEN_EXISTING, true, mFilePart->mOffset, mFilePart->mLength, false };

                const auto bytesWritten = mS3->prepareLocalCacheFile(CONT_CALLER
                    mBucket, mKey, meta);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: prepareLocalCacheFile_Multipart return %lld", bytesWritten);
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


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
bool AwsS3::readFile_Multipart(CALLER_ARG PVOID UParam,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    APP_ASSERT(UParam);

    ReadFileContext* ctx = (ReadFileContext*)UParam;
    APP_ASSERT(!ctx->mBucket.empty());
    APP_ASSERT(!ctx->mKey.empty());
    APP_ASSERT(ctx->mKey.back() != L'/');

    NEW_LOG_BLOCK();

    const auto remotePath{ ctx->getGuardString() };
    traceW(L"HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx->mFile, Offset, Length, remotePath.c_str());

    //
    // 同時に複数のスレッドから異なるオフセットで呼び出されるので
    // 既に mFile に設定されているか判定し、無駄なロックは避ける
    //
    if (ctx->mFile == INVALID_HANDLE_VALUE)
    {
        // ファイル名への参照を登録

        UnprotectedNamedData<Shared_Multipart> unsafeShare(remotePath);

        {
            // ファイル名のロック
            //
            // 複数スレッドから同一ファイルへの同時アクセスは行われない
            // --> ファイルを安全に操作できることを保証

            ProtectedNamedData<Shared_Multipart> safeShare(unsafeShare);

            //
            // 関数先頭でも mFile のチェックをしているが、ロック有無で状況が
            // 変わってくるため、改めてチェックする
            //
            if (ctx->mFile == INVALID_HANDLE_VALUE)
            {
                traceW(L"init mFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                    ctx->mFile, Offset, Length, remotePath.c_str());

                // openFile() 後の初回の呼び出し

                const std::wstring localPath{ mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(remotePath) };

                // ダウンロードが必要か判断

                bool needGet = false;

                if (!shouldDownload(CONT_CALLER ctx->mBucket, ctx->mKey, localPath, &ctx->mFileInfo, &needGet))
                {
                    traceW(L"fault: shouldDownload");
                    return false;
                }

                traceW(L"needGet: %s", needGet ? L"true" : L"false");

                // キャッシュ・ファイルを開き、HANDLE をコンテキストに保存

                ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
                if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                    CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

                const DWORD dwDesiredAccess = ctx->mGrantedAccess | GENERIC_WRITE;
                const DWORD dwCreationDisposition = needGet ? CREATE_ALWAYS : OPEN_EXISTING;

                ctx->mFile = ::CreateFileW(localPath.c_str(),
                    dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, dwCreationDisposition, CreateFlags, NULL);

                if (ctx->mFile == INVALID_HANDLE_VALUE)
                {
                    traceW(L"fault: CreateFileW");
                    return false;
                }

                StatsIncr(_CreateFile);

                if (needGet)
                {
                    // ダウンロードが必要

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

                        ITask* task = new ReadPartTask(this, ctx->mBucket, ctx->mKey, localPath, filePart);
                        APP_ASSERT(task);

                        mDelayedWorker->addTask(CONT_CALLER task, Priority::Low, CanIgnore::No);
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

                    FILETIME ft;
                    WinFileTime100nsToWinFile(ctx->mFileInfo.CreationTime, &ft);

                    FILETIME ftNow;
                    ::GetSystemTimeAsFileTime(&ftNow);

                    if (!::SetFileTime(ctx->mFile, &ft, &ftNow, &ft))
                    {
                        const auto lerr = ::GetLastError();
                        traceW(L"fault: SetFileTime lerr=%ld", lerr);
                        return false;
                    }
                }

                // 属性情報のサイズと比較

                LARGE_INTEGER fileSize;
                if(!::GetFileSizeEx(ctx->mFile, &fileSize))
                {
                    traceW(L"fault: GetFileSizeEx");
                    return false;
                }

                if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
                {
                    traceW(L"fault: no match filesize ");
                    return false;
                }
            }

            // ファイル名のロックを解放
        }

        // ファイル名への参照を解除
    }

    APP_ASSERT(ctx->mFile);
    APP_ASSERT(ctx->mFile != INVALID_HANDLE_VALUE);

    // Offset, Length によりファイルを読む

    OVERLAPPED Overlapped{};
    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        return false;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mFile, Offset, Length, *PBytesTransferred, Offset - ctx->mLastOffset);

    ctx->mLastOffset = Offset;

    return true;
}

// EOF