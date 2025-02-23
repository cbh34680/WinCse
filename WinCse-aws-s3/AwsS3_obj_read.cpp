#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <filesystem>
#include <fstream>
#include <cinttypes>


using namespace WinCseLib;


//
// GetObject() で取得した内容をファイルに出力
//
static bool writeObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& result, const wchar_t* path)
{
    NEW_LOG_BLOCK();

    bool ret = false;

    const auto pbuf = result.GetBody().rdbuf();

    // ファイルサイズ
    auto fileSize = result.GetContentLength();

    // 更新日時
    const auto lastModified = result.GetLastModified().Millis();

    FILETIME ft;
    UtcMillisToWinFileTime(lastModified, &ft);

    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);

    traceW(L"CreateFile: path=%s", path);

    // result の内容をファイルに出力する
    HANDLE hFile = ::CreateFileW(path,
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        traceW(L"fault: CreateFileW");
        goto exit;
    }

    while (fileSize > 0)
    {
        DWORD bytesWritten = 0;
        char buffer[4096] = {};

        // バッファにデータを読み込む
        std::streamsize bytesRead = pbuf->sgetn(buffer, min(fileSize, sizeof(buffer)));

        // ファイルにデータを書き込む
        if (!::WriteFile(hFile, buffer, (DWORD)bytesRead, &bytesWritten, NULL))
        {
            traceW(L"fault: WriteFile");
            goto exit;
        }

        fileSize -= bytesRead;
    }

    if (!::SetFileTime(hFile, &ft, &ftNow, &ft))
    {
        traceW(L"fault: SetFileTime");
        goto exit;
    }

    ::CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    ret = true;

exit:
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hFile);
    }

    traceW(L"return %s", ret ? L"true" : L"false");

    return ret;
}

//
// 引数で指定されたローカル・キャッシュが存在しない、又は 対する s3 オブジェクトの
// 更新日時より古い場合は新たに GetObject() を実行してキャッシュ・ファイルを作成する
//
bool AwsS3::prepareLocalCacheFile(CALLER_ARG const std::wstring& bucket, const std::wstring& key, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();

    bool needGet = false;

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(localPath, ec);

    if (ec)
    {
        // ローカルにキャッシュ・ファイルが存在しない
        APP_ASSERT(!std::filesystem::exists(localPath));

        traceW(L"no local cache");
        needGet = true;
    }
    else
    {
        // ローカルにキャッシュ・ファイルが存在する
        APP_ASSERT(std::filesystem::is_regular_file(localPath));

        // ローカル・キャッシュの作成日時を取得

        FILETIME ftCreate = {};
        if (!HandleToWinFileTimes(localPath, &ftCreate, NULL, NULL))
        {
            traceW(L"fault: HandleToWinFileTimes");
            return false;
        }

        const auto createMillis = WinFileTimeIn100ns(ftCreate);

        // ctx->fileInfo の内容は古い可能性が高いので、改めて HeadObject を実行する

        const auto dirInfo = unsafeHeadObject(CONT_CALLER bucket, key);
        if (!dirInfo)
        {
            traceW(L"fault: unsafeHeadObject");
            return false;
        }

        // ファイルのオープン処理であり、比較すれば頻度は低いはずなのでキャッシュへの保存はしない
        // (ロックが必要となり面倒なので)

        // ローカル・ファイルの更新日時と比較

        //
        // !!! 注意 !!! 更新系の処理が入ったときには改めて考え直す必要がある
        //

        traceW(L"compare: REMOTE=%" PRIu64 " LOCAL=%" PRIu64, dirInfo->FileInfo.CreationTime, createMillis);

        if (dirInfo->FileInfo.CreationTime > createMillis)
        {
            // リモート・ファイルが更新されているので再取得

            traceW(L"detected update remote file");
            needGet = true;
        }
    }

    traceW(L"needGet: %s", needGet ? L"true" : L"false");

    if (needGet)
    {
        traceW(L"create or update cache-file: %s", localPath.c_str());

        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(WC2MB(bucket).c_str());
        request.SetKey(WC2MB(key).c_str());

        const auto outcome = mClient.ptr->GetObject(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: GetObject");
            return false;
        }

        const auto& result = outcome.GetResult();

        // result の内容をファイルに出力し、タイムスタンプを変更する

        if (!writeObjectResultToFile(CONT_CALLER result, localPath.c_str()))
        {
            traceW(L"fault: writeObjectResultToFile");
            return false;
        }

        //::Sleep(10 * 1000);

        traceW(L"cache-file written done.");
    }

    return true;
}

//
// openFIle() が呼ばれたときに CSData として PTFS_FILE_CONTEXT に保存する内部情報
// closeFile() で削除される
//
struct ReadFileContext
{
    std::wstring bucket;
    std::wstring key;
    UINT32 createOptions;
    UINT32 grantedAccess;
    FSP_FSCTL_FILE_INFO fileInfo;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    ReadFileContext(
        const std::wstring& argBucket, const std::wstring& argKey,
        const UINT32 argCreateOptions, const UINT32 argGrantedAccess,
        const FSP_FSCTL_FILE_INFO& argFileInfo) :
        bucket(argBucket), key(argKey), createOptions(argCreateOptions),
        grantedAccess(argGrantedAccess), fileInfo(argFileInfo)
    {
    }

    ~ReadFileContext()
    {
        if (hFile != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(hFile);
        }
    }
};

bool AwsS3::openFile(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    UINT32 CreateOptions, UINT32 GrantedAccess,
    const FSP_FSCTL_FILE_INFO& fileInfo, 
    PVOID* pCSData)
{
    // DoOpen() から呼び出されるが、ファイルを開く=ダウンロードになってしまうため
    // ここでは CSData に情報のみを保存し、DoRead() から呼び出される readFile() で
    // ファイルのダウンロード処理 (キャッシュ・ファイル) を行う。

    ReadFileContext* ctx = new ReadFileContext{ argBucket, argKey, CreateOptions, GrantedAccess, fileInfo };
    APP_ASSERT(ctx);

    *pCSData = (PVOID*)ctx;

    return true;
}

void AwsS3::closeFile(CALLER_ARG PVOID CSData)
{
    APP_ASSERT(CSData);

    ReadFileContext* ctx = (ReadFileContext*)CSData;

    delete ctx;
}

//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
bool AwsS3::readFile(CALLER_ARG PVOID CSData,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(CSData);

    ReadFileContext* ctx = (ReadFileContext*)CSData;

    APP_ASSERT(!ctx->bucket.empty());
    APP_ASSERT(!ctx->key.empty());
    APP_ASSERT(ctx->key.back() != L'/');

    traceW(L"success: HANDLE=%p, Offset=%" PRIu64 " Length=%ul", ctx->hFile, Offset, Length);

    bool ret = false;
    OVERLAPPED Overlapped = { };

    if (ctx->hFile == INVALID_HANDLE_VALUE)
    {
        // openFile() 後の初回の呼び出し

        const std::wstring localPath{ mCacheDir + L'\\' + EncodeFileNameToLocalNameW(ctx->bucket + L'/' + ctx->key) };

        // キャッシュ・ファイルの準備

        if (!prepareLocalCacheFile(CONT_CALLER ctx->bucket, ctx->key, localPath))
        {
            traceW(L"fault: prepareLocalCacheFile");
            goto exit;
        }

        APP_ASSERT(std::filesystem::exists(localPath));

        // キャッシュ・ファイルを開き、HANDLE をコンテキストに保存

        ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
        if (ctx->createOptions & FILE_DELETE_ON_CLOSE)
            CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

        HANDLE hFile = ::CreateFileW(localPath.c_str(),
            ctx->grantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
            OPEN_EXISTING, CreateFlags, 0);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            //return FspNtStatusFromWin32(GetLastError());
            traceW(L"fault: CreateFileW");
            goto exit;
        }

        ctx->hFile = hFile;
    }

    APP_ASSERT(ctx->hFile);
    APP_ASSERT(ctx->hFile != INVALID_HANDLE_VALUE);

    // Offset, Length によりファイルを読む

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->hFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        //return FspNtStatusFromWin32(GetLastError());
        traceW(L"fault: ReadFile");
        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%" PRIu64 " Length=%ul, PBytesTransferred=%ul",
        ctx->hFile, Offset, Length, *PBytesTransferred);

    ret = true;

exit:
    if (!ret)
    {
        ::CloseHandle(ctx->hFile);
        ctx->hFile = INVALID_HANDLE_VALUE;
    }

    return ret;
}

// EOF