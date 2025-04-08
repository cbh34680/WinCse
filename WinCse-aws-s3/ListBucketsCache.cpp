#include "WinCseLib.h"
#include "ListBucketsCache.hpp"
#include <algorithm>
#include <iterator>

using namespace WCSE;


#define LN              L"\n"
#define INDENT1         L"\t"
#define INDENT2         L"\t\t"
#define INDENT3         L"\t\t\t"
#define INDENT4         L"\t\t\t\t"
#define INDENT5         L"\t\t\t\t\t"


static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }


void ListBucketsCache::report(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();

    fwprintf(fp, L"LastGetCallChain=%s" LN, mLastGetCallChain.c_str());
    fwprintf(fp, L"LastSetCallChain=%s" LN, mLastSetCallChain.c_str());
    fwprintf(fp, L"LastClearCallChain=%s" LN, mLastClearCallChain.c_str());

    fwprintf(fp, L"LastGetTime=%s" LN, TimePointToLocalTimeStringW(mLastGetTime).c_str());
    fwprintf(fp, L"LastSetTime=%s" LN, TimePointToLocalTimeStringW(mLastSetTime).c_str());
    fwprintf(fp, L"LastClearTime=%s" LN, TimePointToLocalTimeStringW(mLastClearTime).c_str());

    fwprintf(fp, L"CountGet=%d" LN, mCountGet);
    fwprintf(fp, L"CountSet=%d" LN, mCountSet);
    fwprintf(fp, L"CountClear=%d" LN, mCountClear);

    fwprintf(fp, L"[BucketNames]" LN);
    fwprintf(fp, INDENT1 L"List.size=%zu" LN, mList.size());

    for (const auto& it: mList)
    {
        fwprintf(fp, INDENT2 L"%s" LN, it->FileNameBuf);
    }

    fwprintf(fp, INDENT1 L"[Region Map]" LN);
    fwprintf(fp, INDENT2 L"BucketRegions.size=%zu" LN, mBucketRegions.size());

    for (const auto& it: mBucketRegions)
    {
        fwprintf(fp, INDENT3 L"bucket=[%s] region=[%s]" LN, it.first.c_str(), it.second.c_str());
    }
}

std::chrono::system_clock::time_point ListBucketsCache::getLastSetTime(CALLER_ARG0) const
{
    THREAD_SAFE();

    return mLastSetTime;
}

void ListBucketsCache::set(CALLER_ARG const DirInfoListType& argDirInfoList)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    traceW(L"* argDirInfoList.size()=%zu", argDirInfoList.size());

    mLastSetTime = std::chrono::system_clock::now();
    mLastSetCallChain = CALL_CHAIN();
    mCountSet++;

    mList = argDirInfoList;
}

DirInfoListType ListBucketsCache::get(CALLER_ARG0)
{
    THREAD_SAFE();

    mLastGetTime = std::chrono::system_clock::now();
    mLastGetCallChain = CALL_CHAIN();
    mCountGet++;

    return mList;
}

void ListBucketsCache::clear(CALLER_ARG0)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    traceW(L"* mList.size()=%zu", mList.size());

    mList.clear();

    mLastSetTime = std::chrono::system_clock::time_point{};
    mLastGetTime = std::chrono::system_clock::time_point{};
    mLastSetCallChain = L"";
    mLastGetCallChain = L"";
    mCountSet = 0;
    mCountGet = 0;

    mLastClearTime = std::chrono::system_clock::now();
    mLastClearCallChain = CALL_CHAIN();
    mCountClear++;
}

DirInfoType ListBucketsCache::find(CALLER_ARG const std::wstring& argBucketName)
{
    THREAD_SAFE();
    APP_ASSERT(!argBucketName.empty());

    const auto it = std::find_if(mList.begin(), mList.end(), [&argBucketName](const auto& dirInfo)
    {
        return argBucketName == dirInfo->FileNameBuf;
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

std::wstring ListBucketsCache::getBucketRegion(CALLER_ARG const std::wstring& argBucketName)
{
    THREAD_SAFE();
    APP_ASSERT(!argBucketName.empty());

    const auto it{ mBucketRegions.find(argBucketName) };
    if (it == mBucketRegions.end())
    {
        return L"";
    }

    return it->second.c_str();
}

void ListBucketsCache::addBucketRegion(CALLER_ARG const std::wstring& argBucketName, const std::wstring& argRegion)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(!argRegion.empty());

    traceW(L"* argBucketName=%s, argRegion=%s", argBucketName.c_str(), argRegion.c_str());

    mBucketRegions[argBucketName] = argRegion;
}

// EOF