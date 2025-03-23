#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


//
// GetObject() で取得した内容をファイルに出力
//
// argOffset)
//      -1 以下     書き出しオフセット指定なし
//      それ以外    CreateFile 後に SetFilePointerEx が実行される
//
static int64_t outputObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputParams& argOutputParams)
{
    NEW_LOG_BLOCK();

    // 入力データ
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // ファイルサイズ

    std::vector<char> vbuffer(1024 * 64);

    traceW(argOutputParams.str().c_str());

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
        const std::streamsize bytesRead = pbuf->sgetn(buffer, min(remainingTotal, (int64_t)vbuffer.size()));
        if (bytesRead <= 0)
        {
            traceW(L"fault: Read error");

            return -1LL;
        }

        traceW(L"%lld bytes read", bytesRead);

        // ファイルにデータを書き込む

        char* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            traceW(L"%lld bytes remaining", remainingWrite);

            DWORD bytesWritten = 0;
            if (!::WriteFile(hFile.handle(), pos, (DWORD)remainingWrite, &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: WriteFile lerr=%ld", lerr);

                return -1LL;
            }

            traceW(L"%lld bytes written", bytesWritten);

            pos += bytesWritten;
            remainingWrite -= bytesWritten;
        }

        remainingTotal -= bytesRead;
    }

    traceW(L"return %lld", inputSize);

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
int64_t AwsS3::prepareLocalCacheFile(CALLER_ARG
    const ObjectKey& argObjKey, const FileOutputParams& argOutputParams)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s meta=%s", argObjKey.c_str(), argOutputParams.str().c_str());

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
    traceA("range=%s", range.c_str());

    namespace chrono = std::chrono;
    const chrono::steady_clock::time_point start{ chrono::steady_clock::now() };

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (!range.empty())
    {
        request.SetRange(range);
    }

    const auto outcome = mClient.ptr->GetObject(request);
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

    traceW(L"DOWNLOADTIME argObjKey=%s size=%lld duration=%lld",
        argObjKey.c_str(), bytesWritten, duration.count());

    return bytesWritten;
}

bool AwsS3::syncFileAttributes(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& remoteInfo, const std::wstring& localPath,
    bool* pNeedDownload)
{
    //
    // リモートのファイル属性をローカルのキャッシュ・ファイルに反映する
    // ダウンロードが必要な場合は pNeedDownload により通知
    //
    NEW_LOG_BLOCK();
    APP_ASSERT(pNeedDownload);

    traceW(L"argObjKey=%s localPath=%s", argObjKey.c_str(), localPath.c_str());
    traceW(L"remoteInfo FileSize=%llu LastWriteTime=%llu", remoteInfo.FileSize, remoteInfo.LastWriteTime);
    traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", remoteInfo.CreationTime, remoteInfo.LastWriteTime);

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

    auto lerr = ::GetLastError();

    if (hFile.valid())
    {
        traceW(L"exists: local");

        // ローカル・ファイルが存在する

        if (!HandleToFileInfo(hFile.handle(), &localInfo))
        {
            traceW(L"fault: HandleToFileInfo");
            return false;
        }

        traceW(L"localInfo FileSize=%llu LastWriteTime=%llu", localInfo.FileSize, localInfo.LastWriteTime);
        traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", localInfo.CreationTime, localInfo.LastWriteTime);

        if (remoteInfo.FileSize == localInfo.FileSize &&
            localInfo.LastWriteTime == remoteInfo.LastWriteTime)
        {
            // --> 全て同じなので処理不要

            traceW(L"same file, skip");
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
        if (lerr != ERROR_FILE_NOT_FOUND)
        {
            // 想定しないエラー

            traceW(L"fault: CreateFileW lerr=%lu", lerr);
            return false;
        }

        traceW(L"not exists: local");

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

    traceW(L"syncRemoteTime = %s", BOOL_CSTRW(syncTime));
    traceW(L"truncateLocal = %s", BOOL_CSTRW(truncateFile));
    traceW(L"needDownload = %s", BOOL_CSTRW(needDownload));

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
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            truncateFile ? CREATE_ALWAYS : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile.invalid())
        {
            lerr = ::GetLastError();
            traceW(L"fault: CreateFileW lerr=%lu", lerr);

            return false;
        }

        // 更新日時を同期

        traceW(L"setFileTime");

        if (!hFile.setFileTime(remoteInfo.CreationTime, remoteInfo.LastWriteTime))
        {
            lerr = ::GetLastError();
            traceW(L"fault: setFileTime lerr=%lu", lerr);

            return false;
        }

        if (!HandleToFileInfo(hFile.handle(), &localInfo))
        {
            traceW(L"fault: HandleToFileInfo");
            return false;
        }
    }

    if (!needDownload)
    {
        // ダウンロードが不要な場合は、ローカルにファイルが存在する状態になっているはず

        APP_ASSERT(hFile.valid());
        APP_ASSERT(localInfo.CreationTime);
    }

    *pNeedDownload = needDownload;

    return true;
}

// EOF