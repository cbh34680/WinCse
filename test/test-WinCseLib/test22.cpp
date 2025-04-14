#include "WinCseLib.h"

#include <map>
#include <set>
#include <chrono>
#include <functional>

struct CacheHeadObject
{
    struct CacheValBase
    {
        std::wstring mCreateCallChain;
        std::wstring mLastAccessCallChain;
        std::chrono::system_clock::time_point mCreateTime;
        std::chrono::system_clock::time_point mLastAccessTime;
        int mRefCount = 0;

        CacheValBase() { }
        CacheValBase(CALLER_ARG0)
        {
            mCreateCallChain = mLastAccessCallChain = CALL_CHAIN();
            mCreateTime = mLastAccessTime = std::chrono::system_clock::now();
        }
    };

    struct NegativeCache : public CacheValBase { };
    struct PositiveCache : public CacheValBase
    {
        WCSE::DirInfoType mV;

        //using CacheValBase::CacheValBase;
        PositiveCache(CALLER_ARG const WCSE::DirInfoType& argV)
            :
            CacheValBase(CONT_CALLER0), mV(argV)
        {
        }
    };

    std::mutex mGuard;

    std::map<std::wstring, PositiveCache> mPositive;
    std::map<std::wstring, NegativeCache> mNegative;

    int mGetPositive = 0;
    int mSetPositive = 0;
    int mUpdPositive = 0;

    int mGetNegative = 0;
    int mSetNegative = 0;
    int mUpdNegative = 0;

    void report(CALLER_ARG FILE* fp);
    void clear(CALLER_ARG0);
    
    int deleteByTime(CALLER_ARG std::chrono::system_clock::time_point threshold);
    int deleteByKey(CALLER_ARG const WCSE::ObjectKey& argObjKey);
    bool get(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pV);
    void set(CALLER_ARG const WCSE::ObjectKey& argObjKey, const WCSE::DirInfoType& argV);
    bool isNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey);
    void addNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey);
    
};

using namespace WCSE;

void CacheHeadObject::report(CALLER_ARG FILE*)
{
}

void CacheHeadObject::clear(CALLER_ARG0)
{
    std::lock_guard<std::mutex> lock_{ mGuard };

    mPositive.clear();
    mNegative.clear();
}


template <typename CacheType>
int deleteBy(const std::function<bool(const typename CacheType::iterator&)>& shouldErase, CacheType& cache)
{
    int count = 0;

    for (auto it=cache.begin(); it!=cache.end(); )
    {
        if (shouldErase(it))
        {
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

int CacheHeadObject::deleteByTime(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    std::lock_guard<std::mutex> lock_{ mGuard };
    NEW_LOG_BLOCK();

    const auto OldAccessTime = [&threshold](const auto& it)
    {
        return it->second.mCreateTime < threshold;
    };

    const int delPositive = deleteBy(OldAccessTime, mPositive);
    const int delNegative = deleteBy(OldAccessTime, mNegative);

    traceW(L"* delete records: Positive=%d Negative=%d", delPositive, delNegative);

    return delPositive + delNegative;
}


int CacheHeadObject::deleteByKey(CALLER_ARG const WCSE::ObjectKey& argObjKey)
{
    std::lock_guard<std::mutex> lock_{ mGuard };
    NEW_LOG_BLOCK();

    traceW(L"* argObjKey=%s", argObjKey.c_str());

    const auto EqualObjKey = [&argObjKey](const auto& it)
    {
        return it->first == argObjKey.str();
    };

    // 引数と一致するものをキャッシュから削除

    const int delPositive = deleteBy(EqualObjKey, mPositive);
    const int delNegative = deleteBy(EqualObjKey, mNegative);

    //traceW(L"delete records: Positive=%d Negative=%d", delPositive, delNegative);

    int delPositiveP = 0;
    int delNegativeP = 0;

    const auto parentDirPtr{ argObjKey.toParentDir() };
    if (parentDirPtr)
    {
        const auto& parentDir{ *parentDirPtr };

        const auto EqualParentDir = [&parentDir](const auto& it)
        {
            return it->first == parentDir.str();
        };

        // 引数の親ディレクトリをキャッシュから削除

        delPositiveP = deleteBy(EqualParentDir, mPositive);
        delNegativeP = deleteBy(EqualParentDir, mNegative);

        //traceW(L"delete records: PositiveP=%d NegativeP=%d", delPositiveP, delNegativeP);
    }
    else
    {
        // 引数が "\bucket" の場合はこちらを通過するが
        // 親ディレクトリは存在しないので問題なし
    }

    return delPositive + delNegative + delPositiveP + delNegativeP;
}

// ----------------------- Positive

bool CacheHeadObject::get(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pV)
{
    std::lock_guard<std::mutex> lock_{ mGuard };
    APP_ASSERT(argObjKey.valid());

    const auto it{ mPositive.find(argObjKey.str()) };

    if (it == mPositive.end())
    {
        return false;
    }

    it->second.mLastAccessCallChain = CALL_CHAIN();
    it->second.mLastAccessTime = std::chrono::system_clock::now();
    it->second.mRefCount++;

    mGetPositive++;

    if (pV)
    {
        *pV = it->second.mV;
    }

    return true;
}

void CacheHeadObject::set(CALLER_ARG const WCSE::ObjectKey& argObjKey, const WCSE::DirInfoType& argV)
{
    std::lock_guard<std::mutex> lock_{ mGuard };
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());

    // キャッシュにコピー

    if (mPositive.find(argObjKey.str()) == mPositive.end())
    {
        mSetPositive++;
    }
    else
    {
        mUpdPositive++;
    }

    traceW(L"* argObjKey=%s", argObjKey.c_str());

    auto v = PositiveCache{ CONT_CALLER argV };
    mPositive.emplace(argObjKey.str(), PositiveCache{ CONT_CALLER argV });
}

// ----------------------- Negative

bool CacheHeadObject::isNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey)
{
    std::lock_guard<std::mutex> lock_{ mGuard };
    APP_ASSERT(argObjKey.valid());

    auto it{ mNegative.find(argObjKey.str()) };

    if (it == mNegative.end())
    {
        return false;
    }

    it->second.mLastAccessCallChain = CALL_CHAIN();
    it->second.mLastAccessTime = std::chrono::system_clock::now();
    it->second.mRefCount++;

    mGetNegative++;

    return true;
}

void CacheHeadObject::addNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey)
{
    std::lock_guard<std::mutex> lock_{ mGuard };
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());

    if (mNegative.find(argObjKey.str()) == mNegative.end())
    {
        mSetNegative++;
    }
    else
    {
        mUpdNegative++;
    }

    traceW(L"* argObjKey=%s", argObjKey.c_str());
    mNegative.emplace(argObjKey.str(), NegativeCache{ CONT_CALLER0 });
}
#if 0
#endif

int test22()
{
    CacheHeadObject a;

	return 0;
}
