#include "ExecuteApi.hpp"
#include "aws_sdk_s3.h"
#include <fstream>

using namespace CSELIB;
using namespace CSEDAS3;


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

bool ExecuteApi::isInBucketFilters(const std::wstring& arg) const noexcept
{
    if (mRuntimeEnv->BucketFilters.empty())
    {
        return true;
    }

    const auto it = std::find_if(mRuntimeEnv->BucketFilters.cbegin(), mRuntimeEnv->BucketFilters.cend(), [&arg](const auto& re)
    {
        return std::regex_match(arg, re);
    });

    return it != mRuntimeEnv->BucketFilters.cend();
}

bool ExecuteApi::shouldIgnoreFileName(const std::wstring& arg) const noexcept
{
    // リストの最大数に関連するので、API 実行結果を生成するときにもチェックが必要

    if (mRuntimeEnv->IgnoreFileNamePatterns)
    {
        return std::regex_search(arg, *mRuntimeEnv->IgnoreFileNamePatterns);
    }

    // 正規表現が設定されていない

    return false;
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

bool ExecuteApi::ListBuckets(CALLER_ARG CSELIB::DirInfoPtrList* pDirInfoList) const noexcept
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

    DirInfoPtrList dirInfoList;

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

        const auto creationDateMillis{ bucket.GetCreationDate().Millis() };
        traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMillisToLocalTimeStringW(creationDateMillis).c_str());

        const auto creationDate = UtcMillisToWinFileTime100ns(creationDateMillis);

        FSP_FSCTL_FILE_INFO fileInfo{};

        // バケットはディレクトリ属性で登録

        fileInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY | mRuntimeEnv->DefaultFileAttributes;

        // 各種 FILETIME にはバケットの CreationDate の値を設定

        fileInfo.CreationTime = creationDate;
        fileInfo.LastAccessTime = creationDate;
        fileInfo.LastWriteTime = creationDate;

        const auto dirInfo{ allocBasicDirInfo(bucketName + L'/', FileTypeEnum::Bucket, fileInfo) };
        APP_ASSERT(dirInfo);

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

bool ExecuteApi::HeadObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) const noexcept
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
    if (!SplitObjectKey(argObjKey.key(), nullptr, &filename))
    {
        traceW(L"fault: SplitObjectKey");
        return false;
    }

    const auto& result = outcome.GetResult();

    // ファイルサイズ

    const auto fileSize = result.GetContentLength();

    // ファイル属性

    UINT32 fileAttributes = mRuntimeEnv->DefaultFileAttributes;

    if (argObjKey.meansDir())
    {
        // ディレクトリ

        fileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    }

    if (argObjKey.meansHidden())
    {
        // 隠しファイル

        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    if (fileAttributes == 0)
    {
        // レギュラー・ファイル

        fileAttributes = FILE_ATTRIBUTE_NORMAL;
    }

    // 各種 FILETIME

    const auto lastModifiedMillis = result.GetLastModified().Millis();
    traceW(L"argObjKey=%s, LastModified=%s", argObjKey.c_str(), UtcMillisToLocalTimeStringW(lastModifiedMillis).c_str());
    const auto lastModified = UtcMillisToWinFileTime100ns(lastModifiedMillis);

    auto creationTime   = lastModified;
    auto lastAccessTime = lastModified;
    auto lastWriteTime  = lastModified;

    // メタ・データを FILETIME に反映

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.cend())
    {
        creationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.cend())
    {
        lastWriteTime = std::stoull(metadata.at("wincse-last-write-time"));
    }

    FSP_FSCTL_FILE_INFO fileInfo{};
    fileInfo.FileAttributes = fileAttributes;
    fileInfo.FileSize       = fileSize;
    fileInfo.CreationTime   = creationTime;
    fileInfo.LastAccessTime = lastAccessTime;
    fileInfo.LastWriteTime  = lastWriteTime;

    auto dirInfo = allocBasicDirInfo(filename, argObjKey.toFileType(), fileInfo);
    APP_ASSERT(dirInfo);

    dirInfo->mUserProperties.insert({ L"wincse-last-modified", std::to_wstring(lastModified) });

    if (metadata.find("wincse-client-guid") != metadata.cend())
    {
        dirInfo->mUserProperties.insert({ L"wincse-client-guid", MB2WC(metadata.at("wincse-client-guid")) });
    }

    *pDirInfo = std::move(dirInfo);

    return true;
}

//
// ListObjectsV2 API を実行し結果を引数のポインタの指す変数に保存する
// 引数の条件に合致するオブジェクトが見つからないときは false を返却
//
bool ExecuteApi::ListObjectsV2(CALLER_ARG const ObjectKey& argObjKey,
    bool argDelimiter, int argLimit, DirInfoPtrList* pDirInfoList) const noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);

    traceW(L"argObjKey=%s, argDelimiter=%s, argLimit=%d",
        argObjKey.c_str(), BOOL_CSTRW(argDelimiter), argLimit);

    DirInfoPtrList dirInfoList;

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

    UTC_MILLIS_T commonPrefixTime = UINT64_MAX;
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
            // タイムスタンプが採取できなければデフォルト値を採用

            commonPrefixTime = mRuntimeEnv->DefaultCommonPrefixTime;
        }

        // ディレクトリ名の収集 (CommonPrefix)

        for (const auto& it : result.GetCommonPrefixes())
        {
            const auto fullPath{ MB2WC(it.GetPrefix()) };
            //traceW(L"GetCommonPrefixes: %s", fullPath.c_str());

            // Prefix 部分を取り除く
            // 
            // "dir/"           --> ""
            // "dir/file1.txt"  --> "file1.txt"

            auto key{ fullPath.substr(argKeyLen) };

            auto chkKey{ key };
            if (!chkKey.empty())
            {
                // CommonPrefixes(=ディレクトリ) なので、"/" 終端されている

                APP_ASSERT(chkKey.back() == L'/');

                chkKey.pop_back();
            }

            if (chkKey.empty())
            {
                // ファイル名が空("") のものはディレクトリ・オブジェクトとして扱う

                key = L".";
            }

            if (this->shouldIgnoreFileName(chkKey))
            {
                // 無視するファイル名はスキップ

                continue;
            }

            // ディレクトリと同じファイル名は無視するために保存

            dirNames.insert(chkKey);

            APP_ASSERT(!key.empty());

            // CommonPrefix なので、ディレクトリ・オブジェクトとして登録

            dirInfoList.push_back(this->makeDirInfoOfDir_2(key, commonPrefixTime));

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

        // ファイル名の収集

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

            if (this->shouldIgnoreFileName(key))
            {
                // 無視するファイル名はスキップ

                continue;
            }

            FSP_FSCTL_FILE_INFO fileInfo{};

            UINT32 fileAttributes = mRuntimeEnv->DefaultFileAttributes;

            if (MeansHiddenFile(key))
            {
                // ".", ".." 以外で先頭が "." で始まっているものは隠しファイルの扱い

                fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            if (isDir)
            {
                fileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            else
            {
                fileInfo.FileSize = it.GetSize();
            }

            fileInfo.FileAttributes |= fileAttributes;

            if (fileInfo.FileAttributes == 0)
            {
                fileInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }

            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            fileInfo.CreationTime = lastModified;
            fileInfo.LastAccessTime = lastModified;
            fileInfo.LastWriteTime = lastModified;

            auto dirInfo = allocBasicDirInfo(key, isDir ? FileTypeEnum::DirectoryObject : FileTypeEnum::FileObject, fileInfo);
            APP_ASSERT(dirInfo);

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

    if (FA_IS_DIR(argFileInfo.FileAttributes))
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

static FILEIO_LENGTH_T writeObjectResultToFile(CALLER_ARG const Aws::S3::Model::GetObjectResult& argResult,
    const std::filesystem::path& argOutputPath, FILEIO_LENGTH_T argOffset)
{
    NEW_LOG_BLOCK();

    // ファイルを開き argOffset の位置にポインタを移動

    FileHandle hFile = ::CreateFileW
    (
        argOutputPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile.invalid())
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%lu", lerr);

        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argOffset;

    if (::SetFilePointerEx(hFile.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: SetFilePointerEx lerr=%lu", lerr);

        return -1LL;
    }

    // 取得した内容をファイルに出力

    const auto pbuf = argResult.GetBody().rdbuf();
    const auto contentLen = argResult.GetContentLength();               // ファイルサイズ

    std::vector<char> vbuffer(min(contentLen, FILESIZE_1KiBu * 64));    // 64Kib

    auto remainingTotal = contentLen;

    while (remainingTotal > 0)
    {
        // バッファにデータを読み込む

        char* buffer = vbuffer.data();
        const std::streamsize bytesRead = pbuf->sgetn(buffer, min(remainingTotal, (FILEIO_LENGTH_T)vbuffer.size()));
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
                traceW(L"fault: WriteFile lerr=%lu", lerr);

                return -1LL;
            }

            //traceW(L"%lld bytes written", bytesWritten);

            pos += bytesWritten;
            remainingWrite -= bytesWritten;
        }

        remainingTotal -= bytesRead;
    }

    return contentLen;
}

FILEIO_LENGTH_T ExecuteApi::GetObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey,
    const std::filesystem::path& argOutputPath, FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength) const noexcept
{
    NEW_LOG_BLOCK();

    const auto endOffset = argOffset + argLength - 1;

    std::ostringstream ss;
    ss << "bytes=";
    ss << argOffset;
    ss << "-";
    ss << endOffset;

    const auto range{ ss.str() };
    traceA("range=%s", range.c_str());

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    request.SetRange(range);

    const auto outcome = mS3Client->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetObject");
        return -1LL;
    }

    const auto& result = outcome.GetResult();

    // result の内容をファイルに出力する

    return writeObjectResultToFile(CONT_CALLER result, argOutputPath, argOffset);
}

// EOF