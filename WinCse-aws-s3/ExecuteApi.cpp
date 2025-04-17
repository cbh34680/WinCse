#include "ExecuteApi.hpp"
#include "aws_sdk_s3.h"
#include <fstream>

using namespace WCSE;


ExecuteApi::ExecuteApi(
    const RuntimeEnv* argRuntimeEnv,
    const std::wstring& argRegion,
    const std::wstring& argAccessKeyId,
    const std::wstring& argSecretAccessKey) noexcept
    :
    mRuntimeEnv(argRuntimeEnv)
{
    NEW_LOG_BLOCK();

    // S3 クライアントの生成

    mSdkOptions = std::make_unique<Aws::SDKOptions>();
    Aws::InitAPI(*mSdkOptions);

    auto region{ WC2MB(argRegion) };

    Aws::Client::ClientConfiguration config;
    if (argRegion.empty())
    {
        // とりあえずデフォルト・リージョンとして設定しておく

        region = AWS_DEFAULT_REGION;
    }

    traceA("region=%s", region.c_str());

    // 東京) Aws::Region::AP_NORTHEAST_1;
    // 大阪) Aws::Region::AP_NORTHEAST_3;

    config.region = region;

    Aws::S3::S3Client* client = nullptr;

    if (!argAccessKeyId.empty() && !argSecretAccessKey.empty())
    {
        const Aws::Auth::AWSCredentials credentials{ WC2MB(argAccessKeyId), WC2MB(argSecretAccessKey) };

        client = new Aws::S3::S3Client(credentials, nullptr, config);

        traceW(L"use credentials");
    }
    else
    {
        client = new Aws::S3::S3Client(config);
    }

    APP_ASSERT(client);
    mS3Client = std::unique_ptr<Aws::S3::S3Client>(client);
}

ExecuteApi::~ExecuteApi()
{
    NEW_LOG_BLOCK();

    // デストラクタからも呼ばれるので、再入可能としておくこと

    // AWS S3 処理終了

    if (mSdkOptions)
    {
        traceW(L"aws shutdown");

        Aws::ShutdownAPI(*mSdkOptions);
        mSdkOptions.reset();
    }
}

bool ExecuteApi::Ping(CALLER_ARG0) const
{
    NEW_LOG_BLOCK();

    // S3 接続試験
    traceW(L"Connection test");

    const auto outcome = mS3Client->ListBuckets();
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: ListBuckets");
        return false;
    }

    return true;
}

bool ExecuteApi::ListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);

    Aws::S3::Model::ListBucketsRequest request;

    const auto outcome = mS3Client->ListBuckets(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: ListBuckets");
        return false;
    }

    DirInfoListType dirInfoList;

    const auto& result = outcome.GetResult();

    for (const auto& bucket : result.GetBuckets())
    {
        const auto bucketName{ MB2WC(bucket.GetName()) };

        if (!this->isInBucketFilters(bucketName))
        {
            // バケット名によるフィルタリング

            //traceW(L"%s: is not in filters, skip", bucketName.c_str());
            continue;
        }

        // バケットの作成日時を取得

        const auto creationMillis{ bucket.GetCreationDate().Millis() };
        traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMilliToLocalTimeStringW(creationMillis).c_str());

        const auto FileTime = UtcMillisToWinFileTime100ns(creationMillis);

        // ディレクトリ・エントリを生成

        auto dirInfo = makeDirInfoDir2(bucketName, FileTime);
        APP_ASSERT(dirInfo);

        // バケットは常に読み取り専用
        // --> ディレクトリに対しては意味がない

        //dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;

        dirInfoList.emplace_back(dirInfo);

        // 最大バケット表示数の確認

        if (mRuntimeEnv->MaxDisplayBuckets > 0)
        {
            if (dirInfoList.size() >= mRuntimeEnv->MaxDisplayBuckets)
            {
                break;
            }
        }
    }

    *pDirInfoList = std::move(dirInfoList);

    return true;
}

bool ExecuteApi::GetBucketRegion(CALLER_ARG
    const std::wstring& argBucketName, std::wstring* pBucketRegion) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pBucketRegion);

    //traceW(L"do GetBucketLocation()");

    namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

    Aws::S3::Model::GetBucketLocationRequest request;
    request.SetBucket(WC2MB(argBucketName));

    const auto outcome = mS3Client->GetBucketLocation(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetBucketLocation");
        return false;
    }

    // ロケーションが取得できたとき

    const auto& result = outcome.GetResult();
    const auto& location = result.GetLocationConstraint();

    if (location == Aws::S3::Model::BucketLocationConstraint::NOT_SET)
    {
        traceW(L"location is NOT_SET");
        return false;
    }

    *pBucketRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

    //traceW(L"success, region is %s", bucketRegion.c_str());

    return true;
}

bool ExecuteApi::HeadObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfo);
    //APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = mS3Client->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject の実行時エラー、またはオブジェクトが見つからない

        traceW(L"fault: HeadObject");
        return false;
    }

    std::wstring filename;
    if (!SplitPath(argObjKey.key(), nullptr, &filename))
    {
        traceW(L"fault: SplitPath");
        return false;
    }

    auto dirInfo = makeEmptyDirInfo(filename);
    APP_ASSERT(dirInfo);

    const auto& result = outcome.GetResult();

    const auto fileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTime100ns(result.GetLastModified().Millis());

    UINT64 creationTime = lastModified;
    UINT64 lastAccessTime = lastModified;
    UINT64 lastWriteTime = lastModified;
    UINT32 fileAttributes = mRuntimeEnv->DefaultFileAttributes;

    if (argObjKey.meansDir())
    {
        fileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    }

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.cend())
    {
        creationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.cend())
    {
        lastWriteTime = std::stoull(metadata.at("wincse-last-write-time"));
    }

    if (argObjKey.meansHidden())
    {
        // 隠しファイル

        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    if (fileAttributes == 0)
    {
        fileAttributes = FILE_ATTRIBUTE_NORMAL;
    }

    dirInfo->FileInfo.FileAttributes = fileAttributes;
    dirInfo->FileInfo.FileSize = fileSize;
    dirInfo->FileInfo.AllocationSize = (fileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
    dirInfo->FileInfo.CreationTime = creationTime;
    dirInfo->FileInfo.LastAccessTime = lastAccessTime;
    dirInfo->FileInfo.LastWriteTime = lastWriteTime;
    dirInfo->FileInfo.ChangeTime = lastModified;
    dirInfo->FileInfo.IndexNumber = HashString(argObjKey.str());

    dirInfo->mUserProperties.insert({ L"wincse-last-modified", std::to_wstring(lastModified) });

    if (metadata.find("wincse-client-guid") != metadata.cend())
    {
        dirInfo->mUserProperties.insert(
            { L"wincse-client-guid", MB2WC(metadata.at("wincse-client-guid")) });
    }

    *pDirInfo = std::move(dirInfo);

    return true;
}

//
// ListObjectsV2 API を実行し結果を引数のポインタの指す変数に保存する
// 引数の条件に合致するオブジェクトが見つからないときは false を返却
//
bool ExecuteApi::ListObjectsV2(CALLER_ARG const ObjectKey& argObjKey,
    bool argDelimiter, int argLimit, DirInfoListType* pDirInfoList) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);
    APP_ASSERT(argObjKey.valid());

    traceW(L"argObjKey=%s, argDelimiter=%s, argLimit=%d",
        argObjKey.c_str(), BOOL_CSTRW(argDelimiter), argLimit);

    DirInfoListType dirInfoList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(argObjKey.bucketA());

    if (argDelimiter)
    {
        request.WithDelimiter("/");
    }

    if (argLimit > 0)
    {
        request.SetMaxKeys(argLimit);
    }

    const auto argKeyLen = argObjKey.key().length();
    if (argObjKey.isObject())
    {
        request.SetPrefix(argObjKey.keyA());
    }

    UINT64 commonPrefixTime = UINT64_MAX;
    std::set<std::wstring> dirNames;

    Aws::String continuationToken;                              // Used for pagination.

    do
    {
        if (!continuationToken.empty())
        {
            request.SetContinuationToken(continuationToken);
        }

        const auto outcome = mS3Client->ListObjectsV2(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: ListObjectsV2");

            return false;
        }

        const auto& result = outcome.GetResult();

        // ディレクトリ・エントリのため最初に一番古いタイムスタンプを収集
        // * CommonPrefix にはタイムスタンプがないため

        for (const auto& it : result.GetContents())
        {
            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            if (lastModified < commonPrefixTime)
            {
                commonPrefixTime = lastModified;
            }
        }

        if (commonPrefixTime == UINT64_MAX)
        {
            // タイムスタンプが採取できなければ参照ディレクトリのものを採用

            commonPrefixTime = mRuntimeEnv->DefaultCommonPrefixTime;
        }

        // ディレクトリの収集

        for (const auto& it : result.GetCommonPrefixes())
        {
            const auto fullPath{ MB2WC(it.GetPrefix()) };
            //traceW(L"GetCommonPrefixes: %s", fullPath.c_str());

            // Prefix 部分を取り除く
            // 
            // "dir/"           --> ""
            // "dir/file1.txt"  --> "file1.txt"

            auto key{ fullPath.substr(argKeyLen) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                }
            }

            if (key.empty())
            {
                // ファイル名が空("") のものはディレクトリ・オブジェクトとして扱う

                key = L".";
            }

            APP_ASSERT(!key.empty());

            // ディレクトリと同じファイル名は無視するために保存

            dirNames.insert(key);

            dirInfoList.push_back(makeDirInfoDir2(key, commonPrefixTime));

            if (argLimit > 0)
            {
                if (dirInfoList.size() >= argLimit)
                {
                    goto exit;
                }
            }

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mRuntimeEnv->MaxDisplayObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

                    goto exit;
                }
            }
        }

        // ファイルの収集
        for (const auto& it : result.GetContents())
        {
            bool isDir = false;

            const auto fullPath{ MB2WC(it.GetKey()) };
            //traceW(L"GetContents: %s", fullPath.c_str());

            // Prefix 部分を取り除く
            // 
            // "dir/"           --> ""
            // "dir/file1.txt"  --> "file1.txt"

            auto key{ fullPath.substr(argKeyLen) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                    isDir = true;
                }
            }

            if (key.empty())
            {
                // ファイル名が空("") のものはディレクトリ・オブジェクトとして扱う

                isDir = true;
                key = L".";
            }

            APP_ASSERT(!key.empty());

            if (dirNames.find(key) != dirNames.cend())
            {
                // ディレクトリと同じ名前のファイルは無視

                continue;
            }

            auto dirInfo = makeEmptyDirInfo(key);
            APP_ASSERT(dirInfo);

            UINT32 FileAttributes = mRuntimeEnv->DefaultFileAttributes;

            if (key != L"." && key != L".." && key.at(0) == L'.')
            {
                // ".", ".." 以外で先頭が "." で始まっているものは隠しファイルの扱い

                FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            if (isDir)
            {
                FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            else
            {
                dirInfo->FileInfo.FileSize = it.GetSize();
                dirInfo->FileInfo.AllocationSize = (dirInfo->FileInfo.FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
            }

            dirInfo->FileInfo.FileAttributes |= FileAttributes;

            if (dirInfo->FileInfo.FileAttributes == 0)
            {
                dirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }

            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            dirInfo->FileInfo.CreationTime = lastModified;
            dirInfo->FileInfo.LastAccessTime = lastModified;
            dirInfo->FileInfo.LastWriteTime = lastModified;
            dirInfo->FileInfo.ChangeTime = lastModified;

            dirInfoList.emplace_back(dirInfo);

            if (argLimit > 0)
            {
                if (dirInfoList.size() >= argLimit)
                {
                    // 結果リストが引数で指定した limit に到達

                    goto exit;
                }
            }

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mRuntimeEnv->MaxDisplayObjects)
                {
                    // 結果リストが ini ファイルで指定した最大値に到達

                    traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

                    goto exit;
                }
            }
        }

        continuationToken = result.GetNextContinuationToken();
    } while (!continuationToken.empty());

exit:
    *pDirInfoList = std::move(dirInfoList);

    return true;
}

bool ExecuteApi::DeleteObjects(CALLER_ARG
    const std::wstring& argBucket, const std::list<std::wstring>& argKeys) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKeys.empty());

    Aws::S3::Model::Delete delete_objects;

    for (const auto& it: argKeys)
    {
        Aws::S3::Model::ObjectIdentifier obj;
        obj.SetKey(WC2MB(it));
        delete_objects.AddObjects(obj);
    }

    traceW(L"DeleteObjects bucket=%s keys=%s", argBucket.c_str(), JoinStrings(argKeys, L',', false).c_str());

    Aws::S3::Model::DeleteObjectsRequest request;
    request.SetBucket(WC2MB(argBucket));
    request.SetDelete(delete_objects);

    const auto outcome = mS3Client->DeleteObjects(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObjects");
        return false;
    }

    return true;
}

bool ExecuteApi::DeleteObject(CALLER_ARG const ObjectKey& argObjKey) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    const auto outcome = mS3Client->DeleteObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    return true;
}

bool ExecuteApi::PutObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (FA_IS_DIRECTORY(argFileInfo.FileAttributes))
    {
        // ディレクトリの場合は空のコンテンツ

        APP_ASSERT(!argSourcePath);
    }
    else
    {
        // ファイルの場合はローカル・キャッシュの内容をアップロードする

        const Aws::String filePath{ WC2MB(argSourcePath) };

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>(
            __FUNCTION__,
            filePath.c_str(),
            std::ios_base::in | std::ios_base::binary
        );

        if (!inputData->good())
        {
            const auto lerr = ::GetLastError();

            traceW(L"fault: inputData->good, fail=%s bad=%s, eof=%s, lerr=%lu",
                BOOL_CSTRW(inputData->fail()), BOOL_CSTRW(inputData->bad()), BOOL_CSTRW(inputData->eof()), lerr);

            return false;
        }

        request.SetBody(inputData);
    }

    const auto creationTime{ std::to_string(argFileInfo.CreationTime) };
    const auto lastWriteTime{ std::to_string(argFileInfo.LastWriteTime) };
    const auto clientGuid{ WC2MB(mRuntimeEnv->ClientGuid) };

    request.AddMetadata("wincse-creation-time", creationTime.c_str());
    request.AddMetadata("wincse-last-write-time", lastWriteTime.c_str());
    request.AddMetadata("wincse-client-guid", clientGuid.c_str());

    traceA("creationTime=%s, lastWriteTime=%s, ClientGuid=%s",
        creationTime.c_str(), lastWriteTime.c_str(), clientGuid.c_str());

#if _DEBUG
    if (argSourcePath)
    {
        request.AddMetadata("wincse-debug-source-path", WC2MB(argSourcePath).c_str());
    }

    request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime).c_str());
    request.AddMetadata("wincse-debug-last-access-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastAccessTime).c_str());
#endif

    traceW(L"PutObject argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    const auto outcome = mS3Client->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    traceW(L"success");

    return true;
}

//
// GetObject() で取得した内容をファイルに出力
//
// argOffset)
//      -1 以下     書き出しオフセット指定なし
//      それ以外    CreateFile 後に SetFilePointerEx が実行される
//

static INT64 writeObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputParams& argFOParams)
{
    NEW_LOG_BLOCK();

    traceW(argFOParams.str().c_str());

    // 入力データ
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // ファイルサイズ

    std::vector<char> vbuffer(FILESIZE_1KiBu * 64);       // 64Kib

    // result の内容をファイルに出力する

    auto remainingTotal = inputSize;

    FileHandle hFile = ::CreateFileW
    (
        argFOParams.mPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        argFOParams.mCreationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile.invalid())
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%ld", lerr);

        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argFOParams.mOffset;

    if (::SetFilePointerEx(hFile.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: SetFilePointerEx lerr=%ld", lerr);

        return -1LL;
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

INT64 ExecuteApi::GetObjectAndWriteToFile(CALLER_ARG
    const ObjectKey& argObjKey, const FileOutputParams& argFOParams) const noexcept
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s argFOParams=%s", argObjKey.c_str(), argFOParams.str().c_str());

    std::ostringstream ss;

    if (argFOParams.mLength > 0)
    {
        // mLength が設定されているときはマルチパート時の部分取得

        ss << "bytes=";
        ss << argFOParams.mOffset;
        ss << '-';
        ss << argFOParams.getOffsetEnd();
    }

    const std::string range{ ss.str() };
    //traceA("range=%s", range.c_str());

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (!range.empty())
    {
        request.SetRange(range);
    }

    const auto outcome = mS3Client->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetObject");
        return -1LL;
    }

    const auto& result = outcome.GetResult();

    // result の内容をファイルに出力する

    const auto bytesWritten = writeObjectResultToFile(CONT_CALLER result, argFOParams);

    if (bytesWritten < 0)
    {
        traceW(L"fault: writeObjectResultToFile");
        return -1LL;
    }

    return bytesWritten;
}

// EOF