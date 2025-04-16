#include "CacheListBuckets.hpp"

using namespace WCSE;


static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }


std::chrono::system_clock::time_point CacheListBuckets::getLastSetTime(CALLER_ARG0) const noexcept
{
    THREAD_SAFE();

    const auto copy{ mLastSetTime };

    return copy;
}

bool CacheListBuckets::empty(CALLER_ARG0) const noexcept
{
    THREAD_SAFE();

    const auto copy{ mList.empty() };

    return copy;
}

void CacheListBuckets::set(CALLER_ARG const DirInfoListType& argDirInfoList) noexcept
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    traceW(L"* argDirInfoList.size()=%zu", argDirInfoList.size());

    mLastSetTime = std::chrono::system_clock::now();
    mLastSetCallChain = CALL_CHAIN();
    mCountSet++;

    mList = argDirInfoList;
}

void CacheListBuckets::get(CALLER_ARG WCSE::DirInfoListType* pDirInfoList) const noexcept
{
    THREAD_SAFE();
    APP_ASSERT(pDirInfoList);

    mLastGetTime = std::chrono::system_clock::now();
    mLastGetCallChain = CALL_CHAIN();
    mCountGet++;

    *pDirInfoList = mList;
}

void CacheListBuckets::clear(CALLER_ARG0) noexcept
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

bool CacheListBuckets::find(CALLER_ARG const std::wstring& argBucketName, DirInfoType* pDirInfo) const noexcept
{
    THREAD_SAFE();
    APP_ASSERT(pDirInfo);
    APP_ASSERT(!argBucketName.empty());

    const auto it = std::find_if(mList.cbegin(), mList.cend(), [&argBucketName](const auto& dirInfo)
    {
        return argBucketName == dirInfo->FileNameBuf;
    });

    if (it == mList.cend())
    {
        return false;
    }

    mLastGetTime = std::chrono::system_clock::now();
    mLastGetCallChain = CALL_CHAIN();
    mCountGet++;

    if (pDirInfo)
    {
        *pDirInfo = *it;
    }

    return true;
}

bool CacheListBuckets::getBucketRegion(CALLER_ARG
    const std::wstring& argBucketName, std::wstring* pBucketRegion) const noexcept
{
    THREAD_SAFE();
    APP_ASSERT(!argBucketName.empty());

    const auto it{ mBucketRegions.find(argBucketName) };
    if (it == mBucketRegions.cend())
    {
        return false;
    }

    if (pBucketRegion)
    {
        *pBucketRegion = it->second;
    }

    return true;
}

void CacheListBuckets::addBucketRegion(CALLER_ARG
    const std::wstring& argBucketName, const std::wstring& argBucketRegion) noexcept
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(!argBucketRegion.empty());

    traceW(L"* argBucketName=%s, argBucketRegion=%s", argBucketName.c_str(), argBucketRegion.c_str());

    mBucketRegions[argBucketName] = argBucketRegion;
}


#define LN              L"\n"
#define INDENT1         L"\t"
#define INDENT2         L"\t\t"
#define INDENT3         L"\t\t\t"
#define INDENT4         L"\t\t\t\t"
#define INDENT5         L"\t\t\t\t\t"

void CacheListBuckets::report(CALLER_ARG FILE* fp) const noexcept
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

// EOF