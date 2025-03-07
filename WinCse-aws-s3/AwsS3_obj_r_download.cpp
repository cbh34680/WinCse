#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


//
// GetObject() で取得した内容をファイルに出力
//
// argOffset)
//      -1 以下     書き出しオフセット指定なし
//      それ以外    CreateFile 後に SetFilePointerEx が実行される
//
static int64_t writeObjectResultToFile(CALLER_ARG
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
    HANDLE hFile = INVALID_HANDLE_VALUE;

    auto remainingTotal = inputSize;

    hFile = ::CreateFileW(argMeta.mPath.c_str(),
        GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, argMeta.mCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%ld", lerr);
        goto exit;
    }

    if (argMeta.mSpecifyRange)
    {
        LARGE_INTEGER li{};
        li.QuadPart = argMeta.mOffset;

        if (::SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) == 0)
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
            if (!::WriteFile(hFile, pos, (DWORD)remainingWrite, &bytesWritten, NULL))
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

        if (!::SetFileTime(hFile, &ft, &ftNow, &ft))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFileTime lerr=%ld", lerr);
            return false;
        }
    }

    ret = inputSize;

exit:
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

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
    const std::wstring& argBucket, const std::wstring& argKey, const FileOutputMeta& argMeta)
{
    NEW_LOG_BLOCK();

    traceW(L"bucket=%s key=%s %s", argBucket.c_str(), argKey.c_str(), argMeta.str().c_str());

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
    request.SetBucket(WC2MB(argBucket));
    request.SetKey(WC2MB(argKey));

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

    const auto bytesWritten = writeObjectResultToFile(CONT_CALLER result, argMeta);

    if (bytesWritten < 0)
    {
        traceW(L"fault: writeObjectResultToFile");
        return -1LL;
    }

    const chrono::steady_clock::time_point end{ chrono::steady_clock::now() };
    const auto duration{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };

    traceW(L"DOWNLOADTIME bucket=%s key=%s size=%lld duration=%lld",
        argBucket.c_str(), argKey.c_str(), bytesWritten, duration.count());

    return bytesWritten;
}

bool AwsS3::shouldDownload(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey, const std::wstring& localPath,
    FSP_FSCTL_FILE_INFO* pFileInfo, bool* pNeedGet)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pFileInfo);
    APP_ASSERT(pNeedGet);

    traceW(L"bucket=%s key=%s localPath=%s", argBucket.c_str(), argKey.c_str(), localPath.c_str());

    bool ret = false;
    bool needGet = false;

    // ctx->fileInfo の内容は古い可能性が高いので、改めて HeadObject を実行する

    FSP_FSCTL_FILE_INFO remote{};
    FSP_FSCTL_FILE_INFO local{};

    if (!this->headObject_File_SkipCacheSearch(CONT_CALLER argBucket, argKey, &remote))
    {
        // 失敗する可能性は少ないが、ローカルには存在するので再取得不要

        traceW(L"fault: headObject_File_SkipCacheSearch");
        goto exit;
    }

    // せっかく取得したので、念のため最新の情報を保存しておく
    // (あまり意味は無いと思う)

    *pFileInfo = remote;

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
            // 失敗する可能性は少ないが、属性情報が取得できないので再取得

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
            needGet = true;
        }

        //
        // 以前のキャッシュ作成時にエラーとなっていたら取り直しが必要
        //
        if (remote.FileSize != local.FileSize)
        {
            traceW(L"filesize unmatch remote=%llu local=%llu", remote.FileSize, local.FileSize);
            needGet = true;
        }
    }
    else
    {
        // キャッシュ・ファイルが存在しない

        traceW(L"no cache file");
        needGet = true;
    }

    ret = true;
    *pNeedGet = needGet;

exit:
    return ret;
}

// EOF