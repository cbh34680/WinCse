#pragma once

#include "CSDeviceInternal.h"

// HeadObject, ListObjectsV2 から取得したデータをキャッシュする
// どちらも型が異なるだけ (DirEntryType, DirEntryListType) なのでテンプレートにして
// このファイルの最後でそれぞれの型のクラスを実体化させている
//
// HeadObject の場合
//  [map]
//      キー      値
//      ----------------------------
//      ObjectKey DirEntryType
//
// ListObjectsV2 の場合
//  [map]
//      キー      値
//      ----------------------------
//      ObjectKey DirEntryListType

#pragma warning(push)
#pragma warning(disable : 4100)

#if defined(THREAD_SAFE)
#error "THREAD_SAFFE(): already defined"
#endif

// マクロにする必要性はないが、わかりやすいので

#define THREAD_SAFE()       std::lock_guard<std::mutex> lock_{ mGuard }

namespace CSEDVC
{

template<typename T>
class ObjectCacheTmpl
{
public:
    virtual ~ObjectCacheTmpl() = default;

    struct CacheValue
    {
        mutable std::wstring                            mCreateCallChain;
        mutable std::wstring                            mLastAccessCallChain;
        mutable std::chrono::system_clock::time_point   mCreateTime;
        mutable std::chrono::system_clock::time_point   mLastAccessTime;
        mutable int                                     mRefCount = 0;

        CacheValue(CALLER_ARG0)
        {
            mCreateCallChain = mLastAccessCallChain = CALL_CHAIN();
            mCreateTime = mLastAccessTime = std::chrono::system_clock::now();
        }
    };

    struct NegativeValue : public CacheValue { };
    struct PositiveValue : public CacheValue
    {
        T mV;

        explicit PositiveValue(CALLER_ARG const T& argV)
            :
            CacheValue(CONT_CALLER0),
            mV(argV)
        {
        }
    };

protected:
    std::map<CSELIB::ObjectKey, PositiveValue>  mPositive;
    std::map<CSELIB::ObjectKey, NegativeValue>  mNegative;

    mutable std::mutex                          mGuard;
    mutable int                                 mGetPositive = 0;
    mutable int                                 mSetPositive = 0;
    mutable int                                 mUpdPositive = 0;
    mutable int                                 mGetNegative = 0;
    mutable int                                 mSetNegative = 0;
    mutable int                                 mUpdNegative = 0;

    template <typename CacheDataT>
    int deleteBy(const std::function<bool(const typename CacheDataT::iterator&)>& shouldErase, CacheDataT& cache) const
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

public:
    // 以降は THREAD_SAFE() マクロによる修飾が必要
    // --> report() の実装時には同様のマクロを定義するか、std::lock_guard を使用する

    virtual void coReport(CALLER_ARG FILE* fp) const = 0;

    int coDeleteByTime(CALLER_ARG std::chrono::system_clock::time_point threshold)
    {
        THREAD_SAFE();
        NEW_LOG_BLOCK();

        const auto OldAccessTime = [&threshold](const auto& it)
        {
            return it->second.mCreateTime < threshold;
        };

        const int delPositive = deleteBy(OldAccessTime, mPositive);
        const int delNegative = deleteBy(OldAccessTime, mNegative);

        const int sum = delPositive + delNegative;

        if (sum > 0)
        {
            traceW(L"* delete records: Positive=%d, Negative=%d", delPositive, delNegative);
        }

        return sum;
    }

    int coDeleteByKey(CALLER_ARG const CSELIB::ObjectKey& argObjKey)
    {
        THREAD_SAFE();
        NEW_LOG_BLOCK();

        const auto EqualObjKey = [&argObjKey](const auto& it)
        {
            return it->first == argObjKey;
        };

        // 引数と一致するものをキャッシュから削除

        const int delPositive = deleteBy(EqualObjKey, mPositive);
        const int delNegative = deleteBy(EqualObjKey, mNegative);

        traceW(L"delete records: Positive=%d Negative=%d", delPositive, delNegative);

        int delPositiveP = 0;
        int delNegativeP = 0;

        const auto parentDir{ argObjKey.toParentDir() };
        if (parentDir)
        {
            const auto EqualParentDir = [&parentDir](const auto& it)
            {
                return it->first == *parentDir;
            };

            // 引数の親ディレクトリをキャッシュから削除

            delPositiveP = deleteBy(EqualParentDir, mPositive);
            delNegativeP = deleteBy(EqualParentDir, mNegative);

            traceW(L"delete records: PositiveP=%d NegativeP=%d", delPositiveP, delNegativeP);
        }
        else
        {
            // 引数が "\bucket" の場合はこちらを通過するが
            // 親ディレクトリは存在しないので問題なし
        }

        const int sum = delPositive + delNegative + delPositiveP + delNegativeP;

        if (sum > 0)
        {
            traceW(L"* delete records: argObjKey=%s, Positive=%d, Negative=%d, PositiveP=%d, NegativeP=%d",
                argObjKey.c_str(), delPositive, delNegative, delPositiveP, delNegativeP);
        }

        return sum;
    }

    // ----------------------- Positive

    bool coGet(CALLER_ARG const CSELIB::ObjectKey& argObjKey, T* pV) const
    {
        THREAD_SAFE();

        const auto it{ mPositive.find(argObjKey) };

        if (it == mPositive.cend())
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

    void coSet(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const T& argV)
    {
        THREAD_SAFE();
        NEW_LOG_BLOCK();

        // キャッシュにコピー

        if (mPositive.find(argObjKey) == mPositive.cend())
        {
            mSetPositive++;
        }
        else
        {
            mUpdPositive++;
        }

        traceW(L"* argObjKey=%s", argObjKey.c_str());

        mPositive.emplace(argObjKey, PositiveValue{ CONT_CALLER argV });
    }

    // ----------------------- Negative

    bool coIsNegative(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const
    {
        THREAD_SAFE();

        const auto it{ mNegative.find(argObjKey) };

        if (it == mNegative.cend())
        {
            return false;
        }

        it->second.mLastAccessCallChain = CALL_CHAIN();
        it->second.mLastAccessTime = std::chrono::system_clock::now();
        it->second.mRefCount++;

        mGetNegative++;

        return true;
    }

    void coAddNegative(CALLER_ARG const CSELIB::ObjectKey& argObjKey)
    {
        THREAD_SAFE();
        NEW_LOG_BLOCK();

        if (mNegative.find(argObjKey) == mNegative.cend())
        {
            mSetNegative++;
        }
        else
        {
            mUpdNegative++;
        }

        traceW(L"* argObjKey=%s", argObjKey.c_str());

        mNegative.emplace(argObjKey, NegativeValue{ CONT_CALLER0 });
    }
};

}	// namespace CSEDVC

#undef THREAD_SAFE

#pragma warning(pop)

namespace CSEDVC
{

class CacheHeadObject final : public ObjectCacheTmpl<CSELIB::DirEntryType>
{
public:
    WINCSEDEVICE_API void coReport(CALLER_ARG FILE* fp) const override;
};

class CacheListObjects final : public ObjectCacheTmpl<CSELIB::DirEntryListType>
{
public:
    WINCSEDEVICE_API void coReport(CALLER_ARG FILE* fp) const override;
};

}	// namespace CSEDVC

// EOF