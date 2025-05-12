#include "CacheListBuckets.hpp"

using namespace CSELIB;
using namespace CSESS3;


#if defined(THREAD_SAFE)
#error "THREAD_SAFFE(): already defined"
#endif

#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ mGuard }

std::chrono::system_clock::time_point CacheListBuckets::clbGetLastSetTime(CALLER_ARG0) const
{
    THREAD_SAFE();

    const auto lastSetTime{ mLastSetTime };

    return lastSetTime;
}

bool CacheListBuckets::clbEmpty(CALLER_ARG0) const
{
    THREAD_SAFE();

    const auto listIsEmpty = mList.empty();

    return listIsEmpty;
}

void CacheListBuckets::clbSet(CALLER_ARG const DirEntryListType& argDirEntryList)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    traceW(L"* argDirEntryList.size()=%zu", argDirEntryList.size());

    mLastSetTime = std::chrono::system_clock::now();
    mLastSetCallChain = CALL_CHAIN();
    mCountSet++;

    mList = argDirEntryList;
}

void CacheListBuckets::clbGet(CALLER_ARG DirEntryListType* pDirEntryList) const
{
    THREAD_SAFE();
    APP_ASSERT(pDirEntryList);

    mLastGetTime = std::chrono::system_clock::now();
    mLastGetCallChain = CALL_CHAIN();
    mCountGet++;

    *pDirEntryList = mList;
}

void CacheListBuckets::clbClear(CALLER_ARG0)
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

bool CacheListBuckets::clbFind(CALLER_ARG const std::wstring& argBucketName, DirEntryType* pDirEntry) const
{
    THREAD_SAFE();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    const auto it = std::find_if(mList.cbegin(), mList.cend(), [&argBucketName](const auto& item)
    {
        return argBucketName == item->mName;
    });

    if (it == mList.cend())
    {
        return false;
    }

    mLastGetTime = std::chrono::system_clock::now();
    mLastGetCallChain = CALL_CHAIN();
    mCountGet++;

    if (pDirEntry)
    {
        *pDirEntry = *it;
    }

    return true;
}

bool CacheListBuckets::clbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion) const
{
    THREAD_SAFE();
    APP_ASSERT(pBucketRegion);
    APP_ASSERT(!argBucketName.empty());

    const auto it{ mBucketRegions.find(argBucketName) };
    if (it == mBucketRegions.cend())
    {
        return false;
    }

    *pBucketRegion = it->second;

    return true;
}

void CacheListBuckets::clbAddBucketRegion(CALLER_ARG const std::wstring& argBucketName, const std::wstring& argBucketRegion)
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

void CacheListBuckets::clbReport(CALLER_ARG FILE* fp) const
{
    THREAD_SAFE();

    fwprintf(fp, L"LastGetCallChain=%s"     LN, mLastGetCallChain.c_str());
    fwprintf(fp, L"LastSetCallChain=%s"     LN, mLastSetCallChain.c_str());
    fwprintf(fp, L"LastClearCallChain=%s"   LN, mLastClearCallChain.c_str());

    fwprintf(fp, L"LastGetTime=%s"          LN, TimePointToLocalTimeStringW(mLastGetTime).c_str());
    fwprintf(fp, L"LastSetTime=%s"          LN, TimePointToLocalTimeStringW(mLastSetTime).c_str());
    fwprintf(fp, L"LastClearTime=%s"        LN, TimePointToLocalTimeStringW(mLastClearTime).c_str());

    fwprintf(fp, L"CountGet=%d"             LN, mCountGet);
    fwprintf(fp, L"CountSet=%d"             LN, mCountSet);
    fwprintf(fp, L"CountClear=%d"           LN, mCountClear);

    fwprintf(fp, L"[BucketNames]"           LN);
    fwprintf(fp, INDENT1 L"List.size=%zu"   LN, mList.size());

    for (const auto& it: mList)
    {
        fwprintf(fp, INDENT2 L"%s"                      LN, it->mName.c_str());
    }

    fwprintf(fp, INDENT1 L"[Region Map]"                LN);
    fwprintf(fp, INDENT2 L"BucketRegions.size=%zu"      LN, mBucketRegions.size());

    for (const auto& it: mBucketRegions)
    {
        fwprintf(fp, INDENT3 L"bucket=[%s] region=[%s]" LN, it.first.c_str(), it.second.c_str());
    }
}

// EOF