#include "AwsS3.hpp"

using namespace WCSE;


DirInfoType AwsS3::apicallHeadObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = mClient->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject の実行時エラー、またはオブジェクトが見つからない

        traceW(L"fault: HeadObject");
        return nullptr;
    }

    std::wstring fileName;
    if (!SplitPath(argObjKey.key(), nullptr, &fileName))
    {
        traceW(L"fault: SplitPath");
        return nullptr;
    }

    auto dirInfo = makeDirInfo(fileName);
    APP_ASSERT(dirInfo);

    const auto& result = outcome.GetResult();
    const auto fileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTime100ns(result.GetLastModified().Millis());

    UINT64 creationTime = lastModified;
    UINT64 lastAccessTime = lastModified;
    UINT64 lastWriteTime = lastModified;
    UINT32 fileAttributes = mDefaultFileAttributes;

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.end())
    {
        creationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    //if (metadata.find("wincse-last-access-time") != metadata.end())
    //{
    //    lastAccessTime = std::stoull(metadata.at("wincse-last-access-time"));
    //}

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

    //dirInfo->FileInfo.IndexNumber = HashString(argObjKey.bucket() + L'/' + argObjKey.key());
    dirInfo->FileInfo.IndexNumber = HashString(argObjKey.str());

    return dirInfo;
}

//
// ListObjectsV2 API を実行し結果を引数のポインタの指す変数に保存する
// 引数の条件に合致するオブジェクトが見つからないときは false を返却
//
bool AwsS3::apicallListObjectsV2(CALLER_ARG const ObjectKey& argObjKey, const bool argDelimiter, const int argLimit, DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);
    APP_ASSERT(argObjKey.valid());

    //traceW(L"purpose=%s, argObjKey=%s, delimiter=%s, limit=%d", PurposeString(argPurpose), argObjKey.c_str(), BOOL_CSTRW(delimiter), limit);

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
    if (argObjKey.hasKey())
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

            if (mConfig.maxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mConfig.maxDisplayObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mConfig.maxDisplayObjects);

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

            if (mConfig.maxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mConfig.maxDisplayObjects)
                {
                    // 結果リストが ini ファイルで指定した最大値に到達

                    traceW(L"warning: over max-objects(%d)", mConfig.maxDisplayObjects);

                    goto exit;
                }
            }
        }

        continuationToken = result.GetNextContinuationToken();
    } while (!continuationToken.empty());

exit:
    *pDirInfoList = dirInfoList;

    return true;
}

// EOF