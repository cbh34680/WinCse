#include "WinCseLib.h"
#include "BucketCache.hpp"
#include <algorithm>
#include <iterator>

using namespace WinCseLib;


#define LN              L"\n"
#define INDENT1         L"\t"
#define INDENT2         L"\t\t"
#define INDENT3         L"\t\t\t"
#define INDENT4         L"\t\t\t\t"
#define INDENT5         L"\t\t\t\t\t"

void BucketCache::report(CALLER_ARG FILE* fp)
{
    fwprintf(fp, L"LastSetCallChain=%s" LN, mLastSetCallChain.c_str());
    fwprintf(fp, L"LastGetCallChain=%s" LN, mLastGetCallChain.c_str());
    fwprintf(fp, L"LastSetTime=%s" LN, TimePointToLocalTimeStringW(mLastSetTime).c_str());
    fwprintf(fp, L"LastGetTime=%s" LN, TimePointToLocalTimeStringW(mLastGetTime).c_str());
    fwprintf(fp, L"CountGet=%d" LN, mCountGet);
    fwprintf(fp, L"CountSet=%d" LN, mCountSet);

    fwprintf(fp, L"[BucketNames]" LN);
    fwprintf(fp, INDENT1 L"List.size=%zu" LN, mList.size());

    for (const auto& it: mList)
    {
        fwprintf(fp, INDENT2 L"%s" LN, it->FileNameBuf);
    }

    fwprintf(fp, INDENT1 L"[Region Map]" LN);
    fwprintf(fp, INDENT2 L"RegionMap.size=%zu" LN, mRegionMap.size());

    for (const auto& it: mRegionMap)
    {
        fwprintf(fp, INDENT3 L"bucket=[%s] region=[%s]" LN, it.first.c_str(), it.second.c_str());
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
    const DirInfoListType& dirInfoList)
{
    mList = dirInfoList;
    mLastSetTime = std::chrono::system_clock::now();
    mLastSetCallChain = CALL_CHAIN();
    mCountSet++;
}

void BucketCache::load(CALLER_ARG const std::wstring& region, 
    DirInfoListType& dirInfoList)
{
    const auto& regionMap{ mRegionMap };

    DirInfoListType newList;

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

DirInfoType BucketCache::find(CALLER_ARG const std::wstring& bucketName)
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