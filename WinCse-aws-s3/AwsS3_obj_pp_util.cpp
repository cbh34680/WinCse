#include "AwsS3.hpp"
#include "AwsS3_obj_pp_util.h"

using namespace WCSE;


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
                    mFilePart->mOffset,
                    mFilePart->mLength
                };

                const auto bytesWritten = mAwsS3->apicallGetObjectAndWriteToFile(CONT_CALLER mObjKey, outputParams);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: apicallGetObjectAndWriteToFile bytesWritten=%lld", bytesWritten);
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

    void cancelled(CALLER_ARG0) noexcept
    {
        NEW_LOG_BLOCK();

        traceW(L"set Interrupt");

        mFilePart->mInterrupt = true;
    }
};

//
// 取得データをパートに分けて分割ダウンロードする
//
bool AwsS3::downloadMultipart(CALLER_ARG OpenContext* ctx, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    // 一つのパート・サイズ

    std::list<std::shared_ptr<FilePart>> fileParts;

    // 分割取得する領域を作成

    const int numParts = (int)((ctx->mFileInfo.FileSize + PART_SIZE_BYTE - 1) / PART_SIZE_BYTE);

    auto remaining = ctx->mFileInfo.FileSize;

    for (int i=0; i<numParts; i++)
    {
        fileParts.emplace_back(
            std::make_shared<FilePart>(mStats,
            PART_SIZE_BYTE * i,                       // Offset
            (ULONG)min(PART_SIZE_BYTE, remaining)     // Length
        )
        );

        remaining -= PART_SIZE_BYTE;
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



// EOF