#include "ExecuteApi.hpp"
#include "aws_sdk_s3.h"

using namespace CSELIB;
using namespace CSESS3;


ExecuteApi::ExecuteApi(IWorker* argDelayedWorker, const RuntimeEnv* argRuntimeEnv, Aws::S3::S3Client* argS3Client)
    :
    mDelayedWorker(argDelayedWorker),
    mRuntimeEnv(argRuntimeEnv),
    mS3Client(argS3Client)
{
}

bool ExecuteApi::isInBucketFilters(const std::wstring& argBucket) const
{
    const auto& filters{ mRuntimeEnv->BucketFilters };

    if (filters.empty())
    {
        return true;
    }

    const auto it = std::find_if(filters.cbegin(), filters.cend(), [&argBucket](const auto& item)
    {
        return std::regex_match(argBucket, item);
    });

    return it != filters.cend();
}

bool ExecuteApi::shouldIgnoreFileName(const std::filesystem::path& argWinPath) const
{
    APP_ASSERT(!argWinPath.empty());
    APP_ASSERT(argWinPath.wstring().at(0) == L'\\');

    // リストの最大数に関連するので、API 実行結果を生成するときにもチェックが必要

    if (mRuntimeEnv->IgnoreFileNamePatterns)
    {
        return std::regex_search(argWinPath.wstring(), *mRuntimeEnv->IgnoreFileNamePatterns);
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
        errorW(L"fault: ListBuckets");
        return false;
    }

    return true;
}

bool ExecuteApi::ListBuckets(CALLER_ARG DirEntryListType* pDirEntryList) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntryList);

    Aws::S3::Model::ListBucketsRequest request;

    const auto outcome = mS3Client->ListBuckets(request);
    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: ListBuckets");
        return false;
    }

    DirEntryListType dirEntryList;

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

        auto dirEntry{ DirectoryEntry::makeBucketEntry(bucketName, creationDate) };
        APP_ASSERT(dirEntry);

        dirEntryList.emplace_back(std::move(dirEntry));

        // 最大バケット表示数の確認

        if (mRuntimeEnv->MaxDisplayBuckets > 0)
        {
            if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayBuckets)
            {
                break;
            }
        }
    }

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool ExecuteApi::GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pRegion) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pRegion);

    traceW(L"argBucket=%s", argBucket.c_str());

    namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

    Aws::S3::Model::GetBucketLocationRequest request;
    request.SetBucket(WC2MB(argBucket));

    const auto outcome = mS3Client->GetBucketLocation(request);
    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: GetBucketLocation argBucket=%s", argBucket.c_str());
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

    *pRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

    traceW(L"bucketRegion=%s", pRegion->c_str());

    return true;
}

bool ExecuteApi::HeadObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntry);

    traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = mS3Client->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject の実行時エラー、またはオブジェクトが見つからない

        traceW(L"fault: HeadObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    std::wstring filename;
    if (!SplitObjectKey(argObjKey.key(), nullptr, &filename))
    {
        errorW(L"fault: SplitObjectKey argObjKey=%s", argObjKey.c_str());
        return false;
    }

    const auto& result = outcome.GetResult();

    const auto lastModifiedMillis = result.GetLastModified().Millis();
    traceW(L"argObjKey=%s, LastModified=%s", argObjKey.c_str(), UtcMillisToLocalTimeStringW(lastModifiedMillis).c_str());
    const auto lastModified = UtcMillisToWinFileTime100ns(lastModifiedMillis);

    auto dirEntry = argObjKey.meansDir()
        ? DirectoryEntry::makeDirectoryEntry(filename, lastModified)
        : DirectoryEntry::makeFileEntry(filename, result.GetContentLength(), lastModified);
    APP_ASSERT(dirEntry);

    // メタ・データを FILETIME に反映

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.cend())
    {
        dirEntry->mFileInfo.CreationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-access-time") != metadata.cend())
    {
        dirEntry->mFileInfo.LastAccessTime = std::stoull(metadata.at("wincse-last-access-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.cend())
    {
        dirEntry->mFileInfo.LastWriteTime = std::stoull(metadata.at("wincse-last-write-time"));
    }

    if (metadata.find("wincse-change-time") != metadata.cend())
    {
        dirEntry->mFileInfo.ChangeTime = std::stoull(metadata.at("wincse-change-time"));
    }

    dirEntry->mUserProperties.insert({ L"wincse-last-modified", std::to_wstring(lastModified) });
    dirEntry->mUserProperties.insert({ L"wincse-etag", MB2WC(result.GetETag()) });

    traceW(L"dirEntry=%s", dirEntry->str().c_str());

    *pDirEntry = std::move(dirEntry);

    return true;
}

//
// ListObjectsV2 API を実行し結果を引数のポインタの指す変数に保存する
// 引数の条件に合致するオブジェクトが見つからないときは false を返却
//
bool ExecuteApi::ListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntryList);
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    DirEntryListType dirEntryList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(argObjKey.bucketA());
    request.WithDelimiter("/");

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
            errorW(L"fault: ListObjectsV2 argObjKey=%s", argObjKey.c_str());

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
            const auto keyFull{ MB2WC(it.GetPrefix()) };

            if (keyFull == argObjKey.key())
            {
                // 引数のディレクトリ名と同じ(= "." と同義)は無視
                // --> ここは通過しないが、念のため

                continue;
            }

            // Prefix 部分を取り除く
            // 
            // "dir/"           --> ""              ... 上記で除かれている
            // "dir/subdir/"    --> "subdir/"       ... 以降はこちらが対象

            const auto key{ keyFull.substr(argKeyLen) };

            // CommonPrefixes(=ディレクトリ) なので、"/" 終端されている

            APP_ASSERT(!key.empty());
            APP_ASSERT(key != L"/");
            APP_ASSERT(key.back() == L'/');

            const auto keyWinPath{ argObjKey.append(key).toWinPath() };

            if (this->shouldIgnoreFileName(keyWinPath))
            {
                // 無視するファイル名はスキップ

                traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());

                continue;
            }

            // ディレクトリと同じファイル名は無視するために保存

            dirNames.insert(key.substr(0, key.length() - 1));

            // CommonPrefix なので、ディレクトリ・オブジェクトとして登録

            auto dirEntry{ DirectoryEntry::makeDirectoryEntry(key, commonPrefixTime) };
            APP_ASSERT(dirEntry);

            dirEntryList.push_back(std::move(dirEntry));

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

                    goto exit;
                }
            }
        }

        // ファイル名の収集

        for (const auto& it : result.GetContents())
        {
            const auto keyFull{ MB2WC(it.GetKey()) };

            if (keyFull == argObjKey.key())
            {
                // 引数のディレクトリ名と同じ(= "." と同義)は無視

                continue;
            }

            // Prefix 部分を取り除く
            // 
            // "dir/"           --> ""              ... 上記で除かれている
            // "dir/file1.txt"  --> "file1.txt"     ... 以降はこちらが対象

            const auto key{ keyFull.substr(argKeyLen) };

            APP_ASSERT(!key.empty());
            APP_ASSERT(key.back() != L'/');

            if (dirNames.find(key) != dirNames.cend())
            {
                // ディレクトリと同じ名前のファイルは無視

                traceW(L"exists same name of dir key=%s", key.c_str());

                continue;
            }

            const auto keyWinPath{ argObjKey.append(key).toWinPath() };

            if (this->shouldIgnoreFileName(keyWinPath))
            {
                // 無視するファイル名はスキップ

                traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());

                continue;
            }

            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            auto dirEntry = DirectoryEntry::makeFileEntry(key, it.GetSize(), lastModified);
            APP_ASSERT(dirEntry);

            dirEntryList.emplace_back(std::move(dirEntry));

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayObjects)
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
    traceW(L"dirEntryList.size=%zu", dirEntryList.size());

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool ExecuteApi::DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKeys.empty());

    traceW(L"DeleteObjects argBucket=%s argKeys=%s", argBucket.c_str(), JoinStrings(argKeys, L',', true).c_str());

    Aws::S3::Model::Delete delete_objects;

    for (const auto& it: argKeys)
    {
        Aws::S3::Model::ObjectIdentifier obj;
        obj.SetKey(WC2MB(it));
        delete_objects.AddObjects(obj);
    }

    Aws::S3::Model::DeleteObjectsRequest request;
    request.SetBucket(WC2MB(argBucket));
    request.SetDelete(delete_objects);

    const auto outcome = mS3Client->DeleteObjects(request);

    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: DeleteObjects argBucket=%s argKeys=%s", argBucket.c_str(), JoinStrings(argKeys, L',', true).c_str());
        return false;
    }

    return true;
}

bool ExecuteApi::DeleteObject(CALLER_ARG const ObjectKey& argObjKey) const
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
        errorW(L"fault: DeleteObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    return true;
}

bool ExecuteApi::PutObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    APP_ASSERT(argObjKey.isObject());

    // パートサイズを超えたらマルチパート・アップロード

    const auto PART_SIZE_BYTE = FILESIZE_1MiBll * mRuntimeEnv->TransferWriteSizeMib;

    if (static_cast<FILESIZE_T>(argFileInfo.FileSize) <= PART_SIZE_BYTE || !argSourcePath)
    {
        return this->uploadSimple(CONT_CALLER argObjKey, argFileInfo, argSourcePath);
    }
    else
    {
        return this->uploadMultipart(CONT_CALLER argObjKey, argFileInfo, argSourcePath);
    }
}

static FILEIO_LENGTH_T writeFileFromStream(CALLER_ARG
    const Aws::IOStream& argInputStream, FILEIO_LENGTH_T argInputLength,
    const std::filesystem::path& argOutputPath, FILEIO_OFFSET_T argOutputOffset)
{
    NEW_LOG_BLOCK();

    // ファイルを開き argOffset の位置にポインタを移動

    FileHandle file = ::CreateFileW
    (
        argOutputPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: CreateFileW lerr=%lu argOutputPath=%s argOffset=%lld", lerr, argOutputPath.c_str(), argOutputOffset);

        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argOutputOffset;

    if (::SetFilePointerEx(file.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: SetFilePointerEx lerr=%lu file=%s", lerr, file.str().c_str());

        return -1LL;
    }

    // 取得した内容をファイルに出力

    auto* pbuf = argInputStream.rdbuf();
    auto remainingTotal = argInputLength;

    //std::vector<char> vBuffer(min(argInputLength, FILESIZE_1KiBu * 64));    // 64Kib
    //auto* buffer = vBuffer.data();
    //const FILEIO_LENGTH_T bufferSize = vBuffer.size();

    static thread_local char buffer[FILEIO_BUFFER_SIZE];
    const FILEIO_LENGTH_T bufferSize = _countof(buffer);

    while (remainingTotal > 0)
    {
        // バッファにデータを読み込む

        if (!argInputStream.good())
        {
            errorW(L"fault: no good");
            return -1LL;
        }

        const auto bytesRead = pbuf->sgetn(buffer, min(remainingTotal, bufferSize));
        if (bytesRead <= 0)
        {
            errorW(L"fault: sgetn");
            return -1LL;
        }

        traceW(L"bytesRead=%lld", bytesRead);

        // ファイルにデータを書き込む

        auto* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            traceW(L"remainingWrite=%lld", remainingWrite);

            DWORD bytesWritten;
            if (!::WriteFile(file.handle(), pos, static_cast<DWORD>(remainingWrite), &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                errorW(L"fault: WriteFile lerr=%lu", lerr);

                return -1LL;
            }

            pos += bytesWritten;
            remainingWrite -= bytesWritten;

            traceW(L"bytesWritten=%lu remainingWrite=%lld", bytesWritten, remainingWrite);
        }

        remainingTotal -= bytesRead;

        traceW(L"remainingTotal=%lld", remainingTotal);
    }

    return argInputLength;
}

FILEIO_LENGTH_T ExecuteApi::GetObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey,
    const std::filesystem::path& argOutputPath, FILEIO_LENGTH_T argOffset, FILEIO_LENGTH_T argLength) const
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
        errorW(L"fault: GetObject argObjKey=%s", argObjKey.c_str());
        return -1LL;
    }

    const auto& result{ outcome.GetResult() };

    // result の内容をファイルに出力する

    return writeFileFromStream(CONT_CALLER result.GetBody(), result.GetContentLength(), argOutputPath, argOffset);
}

// EOF