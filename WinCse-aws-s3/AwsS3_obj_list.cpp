#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <cinttypes>


using namespace WinCseLib;


//
// ListObjectsV2 API を実行し結果を引数のポインタの指す変数に保存する
// 引数の条件に合致するオブジェクトが見つからないときは false を返却
//
bool AwsS3::unsafeListObjectsV2(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
    const int limit, const bool delimiter)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);

    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(WC2MB(argBucket).c_str());

    if (delimiter)
    {
        request.WithDelimiter("/");
    }

    const auto argKeyLen = argKey.length();
    if (argKeyLen > 0)
    {
        request.SetPrefix(WC2MB(argKey).c_str());
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
            const auto lastModified = UtcMillisToWinFileTimeIn100ns(obj.GetLastModified().Millis());

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

            auto dirInfo = mallocDirInfoW(key, argBucket);
            APP_ASSERT(dirInfo);

            UINT32 FileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;

            if (key != L"." && key != L".." && key[0] == L'.')
            {
                // 隠しファイル
                FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            dirInfo->FileInfo.FileAttributes = FileAttributes;

            dirInfo->FileInfo.CreationTime = commonPrefixTime;
            dirInfo->FileInfo.LastAccessTime = commonPrefixTime;
            dirInfo->FileInfo.LastWriteTime = commonPrefixTime;
            dirInfo->FileInfo.ChangeTime = commonPrefixTime;

            dirInfoList.push_back(dirInfo);

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

            UINT32 FileAttributes = FILE_ATTRIBUTE_READONLY;

            if (key != L"." && key != L".." && key[0] == L'.')
            {
                // 隠しファイル
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

            const auto lastModified = UtcMillisToWinFileTimeIn100ns(obj.GetLastModified().Millis());

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
    *pDirInfoList = dirInfoList;

    return !dirInfoList.empty();
}

std::shared_ptr<FSP_FSCTL_DIR_INFO> AwsS3::unsafeHeadObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(WC2MB(argBucket).c_str());
    request.SetKey(WC2MB(argKey).c_str());

    const auto outcome = mClient.ptr->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject の実行時エラー、またはオブジェクトが見つからない

        return nullptr;
    }

    const auto& result = outcome.GetResult();

    auto dirInfo = mallocDirInfoW(argKey, argBucket);
    APP_ASSERT(dirInfo);

    const auto FileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTimeIn100ns(result.GetLastModified().Millis());

    UINT32 FileAttributes = FILE_ATTRIBUTE_READONLY;

    if (argKey != L"." && argKey != L".." && argKey[0] == L'.')
    {
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


static std::mutex gGuard;

#define THREAD_SAFE() \
    std::lock_guard<std::mutex> lock_(gGuard)


bool AwsS3::headObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKey.empty());
    APP_ASSERT(argKey.back() != L'/');

    traceW(L"bucket: %s, key: %s", argBucket.c_str(), argKey.c_str());

    std::shared_ptr<FSP_FSCTL_DIR_INFO> dirInfo;

    // ポジティブ・キャッシュを調べる
    {
        // 下で dirInfoList を使っているので、ブロックに入れて回避

        std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

        if (mObjectCache.getPositive(CONT_CALLER argBucket, argKey, -1, false, &dirInfoList))
        {
            // -1 で検索しているので一件のみであるはず

            APP_ASSERT(dirInfoList.size() == 1);

            traceW(L"found in positive-cache");
            dirInfo = dirInfoList[0];
        }
    }

    if (!dirInfo)
    {
        traceW(L"not found in positive-cache");

        // ネガティブ・キャッシュを調べる

        if (mObjectCache.isInNegative(CONT_CALLER argBucket, argKey, -1, false))
        {
            // ネガティブ・キャッシュにある == データは存在しない
            traceW(L"found in negative cache");

            return false;
        }

        // HeadObject API の実行
        traceW(L"do HeadObject");

        dirInfo = unsafeHeadObject(CONT_CALLER argBucket, argKey);
        if (!dirInfo)
        {
            // ネガティブ・キャッシュに登録
            traceW(L"add negative");

            mObjectCache.addNegative(CONT_CALLER argBucket, argKey, -1, false);

            return false;
        }

        // キャッシュにコピー
        {
            // 一件のみのリストにする (キャッシュの形式に合わせる)
            std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList{ dirInfo };

            mObjectCache.setPositive(CONT_CALLER argBucket, argKey, -1, false, dirInfoList);
        }
    }

    if (pFileInfo)
    {
        (*pFileInfo) = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
    const int limit, const bool delimiter)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(argBucket.back() != L'/');

    if (!argKey.empty())
    {
        APP_ASSERT(argKey.back() == L'/');
    }

    traceW(L"bucket: %s, key: %s, limit: %d", argBucket.c_str(), argKey.c_str(), limit);

    // ポジティブ・キャッシュを調べる

    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;
    const bool inCache = mObjectCache.getPositive(CONT_CALLER argBucket, argKey, limit, delimiter, &dirInfoList);

    if (inCache)
    {
        // ポジティブ・キャッシュ中に見つかった
        traceW(L"found in positive-cache");
    }
    else
    {
        traceW(L"not found in positive-cache");

        if (mObjectCache.isInNegative(CONT_CALLER argBucket, argKey, limit, delimiter))
        {
            // ネガティブ・キャッシュ中に見つかった
            traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() の実行
        traceW(L"call doListObjectV2");

        if (!this->unsafeListObjectsV2(CONT_CALLER argBucket, argKey, &dirInfoList, limit, delimiter))
        {
            // 実行時エラー、またはオブジェクトが見つからない
            traceW(L"object not found");

            // ネガティブ・キャッシュに登録
            traceW(L"add negative");
            mObjectCache.addNegative(CONT_CALLER argBucket, argKey, limit, delimiter);

            return false;
        }

        // ポジティブ・キャッシュにコピー

        mObjectCache.setPositive(CONT_CALLER argBucket, argKey, limit, delimiter, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// EOF