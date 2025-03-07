#include "AwsS3.hpp"


using namespace WinCseLib;


DirInfoType AwsS3::apicallHeadObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKey.empty());
    APP_ASSERT(argKey.back() != L'/');

    traceW(L"bucket=%s, key=%s", argBucket.c_str(), argKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(WC2MB(argBucket));
    request.SetKey(WC2MB(argKey));

    const auto outcome = mClient.ptr->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject の実行時エラー、またはオブジェクトが見つからない
        traceW(L"fault: HeadObject");

        return nullptr;
    }

    const auto& result = outcome.GetResult();

    auto dirInfo = mallocDirInfoW(argKey, argBucket);
    APP_ASSERT(dirInfo);

    const auto FileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTime100ns(result.GetLastModified().Millis());

    UINT32 FileAttributes = mDefaultFileAttributes;

    if (argKey != L"." && argKey != L".." && argKey[0] == L'.')
    {
        // ".", ".." 以外で先頭が "." で始まっているものは隠しファイルの扱い

        traceW(L"set hidden");
        FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    dirInfo->FileInfo.FileAttributes = FileAttributes;
    dirInfo->FileInfo.FileSize = FileSize;
    dirInfo->FileInfo.AllocationSize = (FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
    dirInfo->FileInfo.CreationTime = lastModified;
    dirInfo->FileInfo.LastAccessTime = lastModified;
    dirInfo->FileInfo.LastWriteTime = lastModified;
    dirInfo->FileInfo.ChangeTime = lastModified;
    dirInfo->FileInfo.IndexNumber = HashString(argBucket + L'/' + argKey);

    return dirInfo;
}

//
// ListObjectsV2 API を実行し結果を引数のポインタの指す変数に保存する
// 引数の条件に合致するオブジェクトが見つからないときは false を返却
//
bool AwsS3::apicallListObjectsV2(CALLER_ARG const Purpose argPurpose,
    const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);

    bool delimiter = false;
    int limit = 0;

    switch (argPurpose)
    {
        case Purpose::CheckDir:
        {
            // ディレクトリの存在確認の為にだけ呼ばれるはず

            APP_ASSERT(!argKey.empty());
            APP_ASSERT(argKey.back() == L'/');

            limit = 1;

            break;
        }
        case Purpose::Display:
        {
            // DoReadDirectory() からのみ呼び出されるはず

            if (!argKey.empty())
            {
                APP_ASSERT(argKey.back() == L'/');
            }

            delimiter = true;

            break;
        }
        default:
        {
            APP_ASSERT(0);
        }
    }

    traceW(L"purpose=%s, bucket=%s, key=%s, delimiter=%s, limit=%d",
        PurposeString(argPurpose), argBucket.c_str(), argKey.c_str(), delimiter ? L"true" : L"false", limit);

    DirInfoListType dirInfoList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(WC2MB(argBucket));

    if (delimiter)
    {
        request.WithDelimiter("/");
    }

    if (limit > 0)
    {
        request.SetMaxKeys(limit);
    }

    const auto argKeyLen = argKey.length();
    if (argKeyLen > 0)
    {
        request.SetPrefix(WC2MB(argKey));
    }

    UINT64 commonPrefixTime = UINT64_MAX;
    std::set<std::wstring> already;

    Aws::String continuationToken;                              // Used for pagination.

    do
    {
        if (!continuationToken.empty())
        {
            request.SetContinuationToken(continuationToken);
        }

        const auto outcome = mClient.ptr->ListObjectsV2(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: ListObjectsV2");

            return false;
        }

        const auto& result = outcome.GetResult();

        //
        // ディレクトリ・エントリのため最初に一番古いタイムスタンプを収集
        // * CommonPrefix にはタイムスタンプがないため
        //
        for (const auto& obj : result.GetContents())
        {
            const auto lastModified = UtcMillisToWinFileTime100ns(obj.GetLastModified().Millis());

            if (lastModified < commonPrefixTime)
            {
                commonPrefixTime = lastModified;
            }
        }

        if (commonPrefixTime == UINT64_MAX)
        {
            // タイムスタンプが採取できなければ参照ディレクトリのものを採用

            commonPrefixTime = mWorkDirTime;
        }

        // ディレクトリの収集
        for (const auto& obj : result.GetCommonPrefixes())
        {
            const std::string fullPath{ obj.GetPrefix().c_str() };      // Aws::String -> std::string

            traceA("GetCommonPrefixes: %s", fullPath.c_str());

            std::wstring key{ MB2WC(fullPath.substr(argKeyLen)) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                }
            }

            if (key.empty())
            {
                key = L".";
            }

            APP_ASSERT(!key.empty());

            if (std::find(already.begin(), already.end(), key) != already.end())
            {
                traceW(L"%s: already added", key.c_str());
                continue;
            }

            already.insert(key);

            dirInfoList.push_back(mallocDirInfoW_dir(key, argBucket, commonPrefixTime));

            if (limit > 0)
            {
                if (dirInfoList.size() >= limit)
                {
                    goto exit;
                }
            }

            if (mMaxObjects > 0)
            {
                if (dirInfoList.size() >= mMaxObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mMaxObjects);

                    goto exit;
                }
            }
        }

        // ファイルの収集
        for (const auto& obj : result.GetContents())
        {
            const std::string fullPath{ obj.GetKey().c_str() };     // Aws::String -> std::string

            traceA("GetContents: %s", fullPath.c_str());

            bool isDir = false;

            std::wstring key{ MB2WC(fullPath.substr(argKeyLen)) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    isDir = true;
                    key.pop_back();
                }
            }

            if (key.empty())
            {
                isDir = true;
                key = L".";
            }

            APP_ASSERT(!key.empty());

            if (std::find(already.begin(), already.end(), key) != already.end())
            {
                traceW(L"%s: already added", key.c_str());
                continue;
            }

            already.insert(key);

            auto dirInfo = mallocDirInfoW(key, argBucket);
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
                dirInfo->FileInfo.FileSize = obj.GetSize();
                dirInfo->FileInfo.AllocationSize = (dirInfo->FileInfo.FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
            }

            dirInfo->FileInfo.FileAttributes |= FileAttributes;

            const auto lastModified = UtcMillisToWinFileTime100ns(obj.GetLastModified().Millis());

            dirInfo->FileInfo.CreationTime = lastModified;
            dirInfo->FileInfo.LastAccessTime = lastModified;
            dirInfo->FileInfo.LastWriteTime = lastModified;
            dirInfo->FileInfo.ChangeTime = lastModified;

            dirInfoList.push_back(dirInfo);

            if (limit > 0)
            {
                if (dirInfoList.size() >= limit)
                {
                    // 結果リストが引数で指定した limit に到達

                    goto exit;
                }
            }

            if (mMaxObjects > 0)
            {
                if (dirInfoList.size() >= mMaxObjects)
                {
                    // 結果リストが ini ファイルで指定した最大値に到達

                    traceW(L"warning: over max-objects(%d)", mMaxObjects);

                    goto exit;
                }
            }
        }

        continuationToken = result.GetNextContinuationToken();
    } while (!continuationToken.empty());

exit:
    if (argPurpose == Purpose::Display && !dirInfoList.empty())
    {
        //
        // 表示用のリストであり、空ではないので cmd と同じ動きをさせるため
        // ".", ".." が存在しない場合に追加する
        //
        if (!argKey.empty())
        {
            const auto itParent = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
            {
                return wcscmp(dirInfo->FileNameBuf, L"..") == 0;
            });

            if (itParent == dirInfoList.end())
            {
                dirInfoList.insert(dirInfoList.begin(), mallocDirInfoW_dir(L"..", argBucket, commonPrefixTime));
            }
            else
            {
                const auto save{ *itParent };
                dirInfoList.erase(itParent);
                dirInfoList.insert(dirInfoList.begin(), save);
            }
        }

        const auto itCurr = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
        {
            return wcscmp(dirInfo->FileNameBuf, L".") == 0;
        });

        if (itCurr == dirInfoList.end())
        {
            dirInfoList.insert(dirInfoList.begin(), mallocDirInfoW_dir(L".", argBucket, commonPrefixTime));
        }
        else
        {
            const auto save{ *itCurr };
            dirInfoList.erase(itCurr);
            dirInfoList.insert(dirInfoList.begin(), save);
        }

        // ディレクトリ -> ファイルの順に追加されているので並び替えは不要
        // 
        //bool compareDirInfo(const DirInfoType& a, const DirInfoType& b);
        //std::sort(dirInfoList.begin(), dirInfoList.end(), compareDirInfo);
    }

    *pDirInfoList = dirInfoList;

    return !dirInfoList.empty();
}

#define IS_FA_DIR(ptr)        (ptr->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)

bool compareDirInfo(const DirInfoType& a, const DirInfoType& b)
{
    if (IS_FA_DIR(a) && !IS_FA_DIR(b))
    {
        return true;
    }
    if (!IS_FA_DIR(a) && IS_FA_DIR(b))
    {
        return false;
    }
    return wcscmp(a->FileNameBuf, b->FileNameBuf) < 0;
}

// EOF