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
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputMeta& argMeta)
{
    NEW_LOG_BLOCK();

    int64_t ret = -1LL;

    // 入力データ
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // ファイルサイズ

    std::vector<char> vbuffer(1024 * 64);

    traceW(argMeta.str().c_str());

    // result の内容をファイルに出力する

    auto remainingTotal = inputSize;

    FileHandleRAII hFile = ::CreateFileW
    (
        argMeta.mPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        argMeta.mCreationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile.invalid())
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%ld", lerr);
        goto exit;
    }

    if (argMeta.mSpecifyRange)
    {
        LARGE_INTEGER li{};
        li.QuadPart = argMeta.mOffset;

        if (::SetFilePointerEx(hFile.handle(), li, NULL, FILE_BEGIN) == 0)
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFilePointerEx lerr=%ld", lerr);
            goto exit;
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
            goto exit;
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
                goto exit;
            }

            traceW(L"%lld bytes written", bytesWritten);

            pos += bytesWritten;
            remainingWrite -= bytesWritten;
        }

        remainingTotal -= bytesRead;
    }

    if (argMeta.mSetFileTime)
    {
        // タイムスタンプを更新

        const auto lastModified = argResult.GetLastModified().Millis();

        FILETIME ft;
        UtcMillisToWinFileTime(lastModified, &ft);

        FILETIME ftNow;
        ::GetSystemTimeAsFileTime(&ftNow);

        if (!::SetFileTime(hFile.handle(), &ft, &ftNow, &ft))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFileTime lerr=%ld", lerr);
            return false;
        }
    }

    ret = inputSize;

exit:
    hFile.close();

    traceW(L"return %lld", ret);

    return ret;
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
    const ObjectKey& argObjKey, const FileOutputMeta& argMeta)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s meta=%s", argObjKey.c_str(), argMeta.str().c_str());

    std::stringstream ss;

    if (argMeta.mSpecifyRange)
    {
        // オフセットの指定があるときは既存ファイルへの
        // 部分書き込みなので Length も指定されるべきである

        ss << "bytes=";
        ss << argMeta.mOffset;
        ss << '-';
        ss << argMeta.getOffsetEnd();
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

    const auto bytesWritten = outputObjectResultToFile(CONT_CALLER result, argMeta);

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

bool AwsS3::shouldDownload(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& remote, const std::wstring& localPath, bool* pNeedDownload)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pNeedDownload);

    traceW(L"argObjKey=%s localPath=%s", argObjKey.c_str(), localPath.c_str());

    bool ret = false;
    bool needDownload = false;

    FSP_FSCTL_FILE_INFO local{};

    if (std::filesystem::exists(localPath))
    {
        // キャッシュ・ファイルが存在する

        if (!std::filesystem::is_regular_file(localPath))
        {
            traceW(L"fault: is_regular_file");
            goto exit;
        }

        // ローカル・キャッシュの属性情報を取得

        if (!PathToFileInfo(localPath, &local))
        {
            traceW(L"fault: PathToFileInfo");
            goto exit;
        }

        traceW(L"LOCAL: size=%llu create=%s write=%s access=%s",
            local.FileSize,
            WinFileTime100nsToLocalTimeStringW(local.CreationTime).c_str(),
            WinFileTime100nsToLocalTimeStringW(local.LastWriteTime).c_str(),
            WinFileTime100nsToLocalTimeStringW(local.LastAccessTime).c_str()
        );

        traceW(L"REMOTE: size=%llu create=%s",
            remote.FileSize,
            WinFileTime100nsToLocalTimeStringW(remote.CreationTime).c_str());

        // ローカル・ファイルの更新日時と比較

        // TODO:
        // 
        // !!! 注意 !!! 更新系の処理が入ったときには改めて考え直す必要がある
        //
        if (remote.CreationTime > local.CreationTime)
        {
            // リモート・ファイルが更新されているので再取得

            traceW(L"remote file changed");
            needDownload = true;
        }

        //
        // 以前のキャッシュ作成時にエラーとなっていたら取り直しが必要
        //
        if (remote.FileSize != local.FileSize)
        {
            traceW(L"filesize unmatch remote=%llu local=%llu", remote.FileSize, local.FileSize);
            needDownload = true;
        }
    }
    else
    {
        // キャッシュ・ファイルが存在しない

        traceW(L"no cache file");
        needDownload = true;
    }

    ret = true;
    *pNeedDownload = needDownload;

exit:
    return ret;
}

// EOF