#include "WinCseLib.h"
#include "BucketCache.hpp"
#include <algorithm>
#include <iterator>

using namespace WinCseLib;


void BucketCache::report(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    traceW(L"LastSetCallChain=%s", mLastSetCallChain.c_str());
    traceW(L"LastGetCallChain=%s", mLastGetCallChain.c_str());
    traceW(L"LastSetTime=%lld", TimePointToUtcSecs(mLastSetTime));
    traceW(L"LastGetTime=%lld", TimePointToUtcSecs(mLastGetTime));
    traceW(L"CountGet=%d", mCountGet);
    traceW(L"CountSet=%d", mCountSet);

    traceW(L"[BucketNames]");
    {
        traceW(L"List.size=%zu", mList.size());

#pragma warning(suppress: 4456)
        NEW_LOG_BLOCK();

        for (const auto& it: mList)
        {
            traceW(L"%s", it->FileNameBuf);
        }
    }

    traceW(L"[Region Map]");
    {
        traceW(L"RegionMap.size=%zu", mRegionMap.size());

#pragma warning(suppress: 4456)
        NEW_LOG_BLOCK();

        for (const auto& it: mRegionMap)
        {
            traceW(L"bucket=[%s] region=[%s]", it.first.c_str(), it.second.c_str());
        }
    }
}

std::chrono::system_clock::time_point BucketCache::getLastSetTime(CALLER_ARG0) const
{
    return mLastSetTime;
}

void BucketCache::clear(CALLER_ARG0)
{
    mList.clear();
}

bool BucketCache::empty(CALLER_ARG0)
{
    return mList.empty();
}

void BucketCache::save(CALLER_ARG
    const std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>& dirInfoList)
{
    mList = dirInfoList;
    mLastSetTime = std::chrono::system_clock::now();
    mLastSetCallChain = CALL_CHAIN();
    mCountSet++;
}

void BucketCache::load(CALLER_ARG const std::wstring& region, 
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>& dirInfoList)
{
    const auto& regionMap{ mRegionMap };

    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> newList;

    std::copy_if(mList.begin(), mList.end(),
        std::back_inserter(newList), [&regionMap, &region](const auto& dirInfo)
    {
        const auto it{ regionMap.find(dirInfo->FileNameBuf) };

        if (it != regionMap.end())
        {
            if (it->second != region)
            {
                return false;
            }
        }

        return true;
    });

    dirInfoList = std::move(newList);

    mLastGetTime = std::chrono::system_clock::now();
    mLastGetCallChain = CALL_CHAIN();
    mCountGet++;
}

std::shared_ptr<FSP_FSCTL_DIR_INFO> BucketCache::find(CALLER_ARG const std::wstring& bucketName)
{
    APP_ASSERT(!bucketName.empty());

    const auto it = std::find_if(mList.begin(), mList.end(), [&bucketName](const auto& dirInfo)
    {
        return bucketName == dirInfo->FileNameBuf;
    });

    if (it == mList.end())
    {
        return nullptr;
    }

    mLastGetTime = std::chrono::system_clock::now();
    mLastGetCallChain = CALL_CHAIN();
    mCountGet++;

    return *it;
}

bool BucketCache::findRegion(CALLER_ARG const std::wstring& bucketName, std::wstring* pBucketRegion)
{
    APP_ASSERT(!bucketName.empty());
    APP_ASSERT(pBucketRegion);

    const auto it{ mRegionMap.find(bucketName) };
    if (it == mRegionMap.end())
    {
        return false;
    }

    *pBucketRegion = it->second.c_str();

    return true;
}

void BucketCache::updateRegion(CALLER_ARG const std::wstring& bucketName, const std::wstring& bucketRegion)
{
    APP_ASSERT(!bucketName.empty());
    APP_ASSERT(!bucketRegion.empty());

    mRegionMap[bucketName] = bucketRegion;
}

// EOF