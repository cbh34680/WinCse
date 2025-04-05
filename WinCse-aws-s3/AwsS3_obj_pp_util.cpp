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
bool AwsS3::doMultipartDownload(CALLER_ARG OpenContext* ctx, const std::wstring& localPath)
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

//
// GetObject() で取得した内容をファイルに出力
//
// argOffset)
//      -1 以下     書き出しオフセット指定なし
//      それ以外    CreateFile 後に SetFilePointerEx が実行される
//
static INT64 outputObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputParams& argOutputParams)
{
    NEW_LOG_BLOCK();

    traceW(argOutputParams.str().c_str());

    // 入力データ
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // ファイルサイズ

    std::vector<char> vbuffer(1024 * 64);       // 64Kib

    // result の内容をファイルに出力する

    auto remainingTotal = inputSize;

    FileHandle hFile = ::CreateFileW
    (
        argOutputParams.mPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        argOutputParams.mCreationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile.invalid())
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%ld", lerr);

        return -1LL;
    }

    if (argOutputParams.mSpecifyRange)
    {
        LARGE_INTEGER li{};
        li.QuadPart = argOutputParams.mOffset;

        if (::SetFilePointerEx(hFile.handle(), li, NULL, FILE_BEGIN) == 0)
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFilePointerEx lerr=%ld", lerr);

            return -1LL;
        }
    }

    while (remainingTotal > 0)
    {
        // バッファにデータを読み込む

        char* buffer = vbuffer.data();
        const std::streamsize bytesRead = pbuf->sgetn(buffer, min(remainingTotal, (INT64)vbuffer.size()));
        if (bytesRead <= 0)
        {
            traceW(L"fault: Read error");

            return -1LL;
        }

        //traceW(L"%lld bytes read", bytesRead);

        // ファイルにデータを書き込む

        char* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            //traceW(L"%lld bytes remaining", remainingWrite);

            DWORD bytesWritten = 0;
            if (!::WriteFile(hFile.handle(), pos, (DWORD)remainingWrite, &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: WriteFile lerr=%ld", lerr);

                return -1LL;
            }

            //traceW(L"%lld bytes written", bytesWritten);

            pos += bytesWritten;
            remainingWrite -= bytesWritten;
        }

        remainingTotal -= bytesRead;
    }

    //traceW(L"return %lld", inputSize);

    return inputSize;
}

//
// 引数で指定されたローカル・キャッシュが存在しない、又は 対する s3 オブジェクトの
// 更新日時より古い場合は新たに GetObject() を実行してキャッシュ・ファイルを作成する
// 
// argOffset)
//      -1 以下     書き出しオフセット指定なし
//      それ以外    CreateFile 後に SetFilePointerEx が実行される
//

INT64 AwsS3::getObjectAndWriteToFile(CALLER_ARG
    const ObjectKey& argObjKey, const FileOutputParams& argOutputParams)
{
    NEW_LOG_BLOCK();

    //traceW(L"argObjKey=%s meta=%s", argObjKey.c_str(), argOutputParams.str().c_str());

    std::stringstream ss;

    if (argOutputParams.mSpecifyRange)
    {
        // オフセットの指定があるときは既存ファイルへの
        // 部分書き込みなので Length も指定されるべきである

        ss << "bytes=";
        ss << argOutputParams.mOffset;
        ss << '-';
        ss << argOutputParams.getOffsetEnd();
    }

    const std::string range{ ss.str() };
    //traceA("range=%s", range.c_str());

    namespace chrono = std::chrono;
    const chrono::steady_clock::time_point start{ chrono::steady_clock::now() };

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (!range.empty())
    {
        request.SetRange(range);
    }

    const auto outcome = mClient->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetObject");
        return -1LL;
    }

    const auto& result = outcome.GetResult();

    // result の内容をファイルに出力する

    const auto bytesWritten = outputObjectResultToFile(CONT_CALLER result, argOutputParams);

    if (bytesWritten < 0)
    {
        traceW(L"fault: outputObjectResultToFile");
        return -1LL;
    }

    const chrono::steady_clock::time_point end{ chrono::steady_clock::now() };
    const auto duration{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };

    //traceW(L"DOWNLOADTIME argObjKey=%s size=%lld duration=%lld", argObjKey.c_str(), bytesWritten, duration.count());

    return bytesWritten;
}

// EOF