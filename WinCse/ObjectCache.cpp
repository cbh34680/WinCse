#include "WinCseLib.h"
#include "ObjectCache.hpp"
#include <filesystem>
#include <mutex>
#include <inttypes.h>


//
// オブジェクト・キャッシュは headObject, listObjects で作成され、呼び出し時の
// パラメータをキーとして保存している。
// これらは DoGetSecurityByName, DoOpen を経由した FileNameToFileInfo により
// 主に呼び出されている。
// 
// 目的に応じて limit, delimiter の値は異なるが、主に以下のような感じになる
// 
//      limit=-1, delimiter=false ... ファイルの存在確認、属性取得         DoGetSecurityByName, DoOpen -> headObject, listObjects
//      limit=1,  delimiter=false ... ディレクトリの存在確認、属性取得     DoGetSecurityByName, DoOpen -> headObject, listObjects
//      limit=0,  delimiter=true  ... ディレクトリ中のオブジェクト一覧     DoReadDirectory             -> listObjects
// 
// キャッシュは上記のキーに紐づいたオブジェクトの一覧なので
// 一件のみ保存しているときも FSP_FSCTL_DIR_INFO のリストとなる。
//

template <typename T>
int eraseByTime(T& cache, std::chrono::system_clock::time_point threshold)
{
    int count = 0;

    for (auto it=cache.begin(); it!=cache.end(); )
    {
        // 最終アクセス時間が引数より小さい場合は削除

        if (it->second.lastAccessTime < threshold)
        {
            //traceW(L"delete Positive) key=[%s][%s] limit=%d delimiter=%s", it->first.bucket.c_str(), it->first.key.c_str(), it->first.limit, it->first.delimiter ? L"true" : L"false");
            it = cache.erase(it);
            count++;
        }
        else
        {
            ++it;
        }
    }

    return count;
}


static std::mutex gGuard;

#define THREAD_SAFE() \
    std::lock_guard<std::mutex> lock_(gGuard)


void ObjectCache::report(CALLER_ARG0)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    traceW(L"GetPositive=%d", mGetPositive);
    traceW(L"SetPositive=%d", mSetPositive);
    traceW(L"GetNegative=%d", mGetNegative);
    traceW(L"SetNegative=%d", mSetNegative);

    traceW(L"[PositiveCache]");
    {
        traceW(L"Positive.size=%zu", mPositive.size());

        NEW_LOG_BLOCK();

        for (const auto& it: mPositive)
        {
            traceW(L"bucket=[%s] key=[%s] limit=%d delimiter=%s",
                it.first.bucket.c_str(), it.first.key.c_str(), it.first.limit, it.first.delimiter ? L"true" : L"false");

            {
                NEW_LOG_BLOCK();
                traceW(L"refCount=%d", it.second.refCount);
                traceW(L"lastCallChain=%s", it.second.lastCallChain.c_str());
                traceW(L"lastAccessTime=%lld", TimePointToUtcSeconds(it.second.lastAccessTime));

                traceW(L"[dirInfoList]");
                {
                    traceW(L"dirInfoList.size=%zu", it.second.dirInfoList.size());

                    NEW_LOG_BLOCK();

                    for (const auto& dirInfo: it.second.dirInfoList)
                    {
                        traceW(L"FileNameBuf=[%s]", dirInfo->FileNameBuf);
                        {
                            NEW_LOG_BLOCK();

                            traceW(L"FileSize=%" PRIu64, dirInfo->FileInfo.FileSize);
                            traceW(L"FileAttributes=%u", dirInfo->FileInfo.FileAttributes);
                            traceW(L"CreationTime=%" PRIu64, dirInfo->FileInfo.CreationTime);
                        }
                    }
                }
            }
        }
    }

    traceW(L"[NegativeCache]");
    {
        traceW(L"mNegative.size=%zu", mNegative.size());

        NEW_LOG_BLOCK();

        for (const auto& it: mNegative)
        {
            traceW(L"bucket=[%s] key=[%s] limit=%d delimiter=%s",
                it.first.bucket.c_str(), it.first.key.c_str(), it.first.limit, it.first.delimiter ? L"true" : L"false");

            {
                NEW_LOG_BLOCK();
                traceW(L"refCount=%d", it.second.refCount);
                traceW(L"lastCallChain=%s", it.second.lastCallChain.c_str());
                traceW(L"lastAccessTime=%lld", TimePointToUtcSeconds(it.second.lastAccessTime));
            }
        }
    }
}

int ObjectCache::deleteOldRecords(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    const int delPositive = eraseByTime(mPositive, threshold);
    const int delNegative = eraseByTime(mNegative, threshold);

    traceW(L"delete records: Positive=%d Negative=%d", delPositive, delNegative);

    return delPositive + delNegative;
}

bool ObjectCache::getPositive(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    const int limit, const bool delimiter,
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList)
{
    THREAD_SAFE();
    APP_ASSERT(pDirInfoList);

    const ObjectCacheKey cacheKey{ argBucket, argKey, limit, delimiter };
    const auto it{ mPositive.find(cacheKey) };

    if (it == mPositive.end())
    {
        return false;
    }

    *pDirInfoList = it->second.dirInfoList;

    it->second.lastAccessTime = std::chrono::system_clock::now();
    it->second.refCount++;
    mGetPositive++;

    return true;
}

void ObjectCache::setPositive(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    const int limit, const bool delimiter,
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>& dirInfoList)
{
    THREAD_SAFE();

    APP_ASSERT(!dirInfoList.empty());

    if (limit == -1)
    {
        // "\\bucket\\dir\\file.txt"

        APP_ASSERT(!argKey.empty());
        APP_ASSERT(argKey.back() != L'/');
        APP_ASSERT(dirInfoList.size() == 1);
    }
    else
    {
        // limit: 0, 1

        if (argKey.empty())
        {
            // "\\bucket"

            APP_ASSERT(limit == 0);
        }
        else
        {
            // "\\bucket\\dir"

            APP_ASSERT(argKey.back() == L'/');
        }
    }


    // キャッシュにコピー

    const ObjectCacheKey cacheKey{ argBucket, argKey, limit, delimiter };
    const PosisiveCacheVal cacheVal{ CONT_CALLER dirInfoList };

    //mPositive[cacheKey] = cacheVal;
    mPositive.emplace(cacheKey, cacheVal);
    mSetPositive++;
}

bool ObjectCache::isInNegative(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    const int limit, const bool delimiter)
{
    THREAD_SAFE();

    const ObjectCacheKey cacheKey{ argBucket, argKey, limit, delimiter };
    const auto it{ mNegative.find(cacheKey) };

    if (it == mNegative.end())
    {
        return false;
    }

    it->second.lastAccessTime = std::chrono::system_clock::now();
    it->second.refCount++;
    mGetNegative++;

    return true;
}

void ObjectCache::addNegative(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    const int limit, const bool delimiter)
{
    THREAD_SAFE();

    // キャッシュにコピー

    const ObjectCacheKey cacheKey{ argBucket, argKey, limit, delimiter };
    const NegativeCacheVal cacheVal{ CONT_CALLER0 };

    mNegative.emplace(cacheKey, cacheVal);
    mSetNegative++;
}

// EOF