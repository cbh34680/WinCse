#include "AwsS3.hpp"
#include <fstream>

using namespace WCSE;


//
// FileOutputParams
//
std::wstring FileOutputParams::str() const
{
    std::wstring sCreationDisposition;

    switch (mCreationDisposition)
    {
        case CREATE_ALWAYS:     sCreationDisposition = L"CREATE_ALWAYS";     break;
        case CREATE_NEW:        sCreationDisposition = L"CREATE_NEW";        break;
        case OPEN_ALWAYS:       sCreationDisposition = L"OPEN_ALWAYS";       break;
        case OPEN_EXISTING:     sCreationDisposition = L"OPEN_EXISTING";     break;
        case TRUNCATE_EXISTING: sCreationDisposition = L"TRUNCATE_EXISTING"; break;
        default: APP_ASSERT(0);
    }

    std::wostringstream ss;

    ss << L"mPath=";
    ss << mPath;
    ss << L" mCreationDisposition=";
    ss << sCreationDisposition;
    ss << L" mOffset=";
    ss << mOffset;
    ss << L" mLength=";
    ss << mLength;

    return ss.str();
}

bool AwsS3A::apicallListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);

    Aws::S3::Model::ListBucketsRequest request;

    const auto outcome = mClient->ListBuckets(request);
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

        auto dirInfo = makeDirInfo_dir(bucketName, FileTime);
        APP_ASSERT(dirInfo);

        // バケットは常に読み取り専用
        // --> ディレクトリに対しては意味がない

        //dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;

        dirInfoList.emplace_back(dirInfo);

        // 最大バケット表示数の確認

        if (mSettings->maxDisplayBuckets > 0)
        {
            if (dirInfoList.size() >= mSettings->maxDisplayBuckets)
            {
                break;
            }
        }
    }

    *pDirInfoList = std::move(dirInfoList);

    return true;
}

bool AwsS3A::apicallGetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion)
{
    NEW_LOG_BLOCK();

    //traceW(L"do GetBucketLocation()");

    namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

    Aws::S3::Model::GetBucketLocationRequest request;
    request.SetBucket(WC2MB(argBucketName));

    const auto outcome = mClient->GetBucketLocation(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetBucketLocation");
        return false;
    }

    // ロケーションが取得できたとき

    const auto& result = outcome.GetResult();
    const auto& location = result.GetLocationConstraint();

    *pBucketRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

    //traceW(L"success, region is %s", bucketRegion.c_str());

    return true;
}

bool AwsS3A::apicallHeadObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfo);
    //APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = mClient->HeadObject(request);
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

    auto dirInfo = makeDirInfo(filename);
    APP_ASSERT(dirInfo);

    const auto& result = outcome.GetResult();

    const auto fileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTime100ns(result.GetLastModified().Millis());

    UINT64 creationTime = lastModified;
    UINT64 lastAccessTime = lastModified;
    UINT64 lastWriteTime = lastModified;
    UINT32 fileAttributes = mDefaultFileAttributes;

    if (argObjKey.meansDir())
    {
        fileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    }

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.end())
    {
        creationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.end())
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

    *pDirInfo = std::move(dirInfo);

    return true;
}

//
// ListObjectsV2 API を実行し結果を引数のポインタの指す変数に保存する
// 引数の条件に合致するオブジェクトが見つからないときは false を返却
//
bool AwsS3A::apicallListObjectsV2(CALLER_ARG const ObjectKey& argObjKey,
    bool argDelimiter, int argLimit, DirInfoListType* pDirInfoList)
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

        const auto outcome = mClient->ListObjectsV2(request);
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

            commonPrefixTime = mWorkDirCTime;
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

            dirInfoList.push_back(makeDirInfo_dir(key, commonPrefixTime));

            if (argLimit > 0)
            {
                if (dirInfoList.size() >= argLimit)
                {
                    goto exit;
                }
            }

            if (mSettings->maxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mSettings->maxDisplayObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mSettings->maxDisplayObjects);

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

            if (dirNames.find(key) != dirNames.end())
            {
                // ディレクトリと同じ名前のファイルは無視

                continue;
            }

            auto dirInfo = makeDirInfo(key);
            APP_ASSERT(dirInfo);

            UINT32 FileAttributes = mDefaultFileAttributes;

            if (key != L"." && key != L".." && key[0] == L'.')
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

            if (mSettings->maxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mSettings->maxDisplayObjects)
                {
                    // 結果リストが ini ファイルで指定した最大値に到達

                    traceW(L"warning: over max-objects(%d)", mSettings->maxDisplayObjects);

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

bool AwsS3A::apicallDeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys)
{
    NEW_LOG_BLOCK();

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

    const auto outcome = mClient->DeleteObjects(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObjects");
        return false;
    }

    return true;
}

bool AwsS3A::apicallDeleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    const auto outcome = mClient->DeleteObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    return true;
}

bool AwsS3A::apicallPutObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, const std::wstring& argFilePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argFilePath=%s", argObjKey.c_str(), argFilePath.c_str());

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (FA_IS_DIRECTORY(argFileInfo.FileAttributes))
    {
        // ディレクトリの場合は空のコンテンツ
    }
    else
    {
        // ファイルの場合はローカル・キャッシュの内容をアップロードする

        const Aws::String filePath{ WC2MB(argFilePath) };

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

    const auto sCreationTime{ std::to_string(argFileInfo.CreationTime) };
    const auto sLastWriteTime{ std::to_string(argFileInfo.LastWriteTime) };

    request.AddMetadata("wincse-creation-time", sCreationTime.c_str());
    request.AddMetadata("wincse-last-write-time", sLastWriteTime.c_str());

    traceA("sCreationTime=%s", sCreationTime.c_str());
    traceA("sLastWriteTime=%s", sLastWriteTime.c_str());

#if _DEBUG
    request.AddMetadata("wincse-debug-source-path", WC2MB(argFilePath).c_str());
    request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime).c_str());
#endif

    traceW(L"PutObject argObjKey=%s, argFilePath=%s", argObjKey.c_str(), argFilePath.c_str());

    const auto outcome = mClient->PutObject(request);

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

static INT64 outputObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputParams& argFOParams)
{
    NEW_LOG_BLOCK();

    traceW(argFOParams.str().c_str());

    // 入力データ
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // ファイルサイズ

    std::vector<char> vbuffer(1024 * 64);       // 64Kib

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

INT64 AwsS3A::apicallGetObjectAndWriteToFile(CALLER_ARG
    const ObjectKey& argObjKey, const FileOutputParams& argFOParams)
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

    const auto outcome = mClient->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetObject");
        return -1LL;
    }

    const auto& result = outcome.GetResult();

    // result の内容をファイルに出力する

    const auto bytesWritten = outputObjectResultToFile(CONT_CALLER result, argFOParams);

    if (bytesWritten < 0)
    {
        traceW(L"fault: outputObjectResultToFile");
        return -1LL;
    }

    return bytesWritten;
}

// EOF