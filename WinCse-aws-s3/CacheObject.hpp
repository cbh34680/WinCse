#pragma once

#include "WinCseLib.h"

// HeadObject, ListObjectsV2 ����擾�����f�[�^���L���b�V������
// �ǂ�����^���قȂ邾�� (DirInfoType, DirInfoListType) �Ȃ̂Ńe���v���[�g�ɂ���
// ���̃t�@�C���̍Ō�ł��ꂼ��̌^�̃N���X�����̉������Ă���
//
// HeadObject �̏ꍇ
//  [unordered_map]
//      �L�[      �l
//      ----------------------------
//      ObjectKey DirInfoType
//
// ListObjectsV2 �̏ꍇ
//  [unordered_map]
//      �L�[      �l
//      ----------------------------
//      ObjectKey DirInfoListType

#include <set>
#include <chrono>
#include <functional>

#pragma warning(push)
#pragma warning(disable : 4100)

#if defined(THREAD_SAFE)
#error "THREAD_SAFFE(): already defined"
#endif

// �}�N���ɂ���K�v���͂Ȃ����A�킩��₷���̂�

#define THREAD_SAFE()       std::lock_guard<std::mutex> lock_{ mGuard }

template<typename T>
class ObjectCacheTmpl
{
public:
    virtual ~ObjectCacheTmpl() = default;

	struct CacheValue
	{
		std::wstring mCreateCallChain;
		std::wstring mLastAccessCallChain;
		std::chrono::system_clock::time_point mCreateTime;
		std::chrono::system_clock::time_point mLastAccessTime;
		int mRefCount = 0;

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
            CacheValue(CONT_CALLER0), mV(argV)
        {
        }
	};

protected:
	std::mutex mGuard;

	std::unordered_map<WCSE::ObjectKey, PositiveValue> mPositive;
	std::unordered_map<WCSE::ObjectKey, NegativeValue> mNegative;

	int mGetPositive = 0;
	int mSetPositive = 0;
	int mUpdPositive = 0;

	int mGetNegative = 0;
	int mSetNegative = 0;
	int mUpdNegative = 0;

    template <typename CacheDataT>
    int deleteBy(
        const std::function<bool(const typename CacheDataT::iterator&)>& shouldErase,
        CacheDataT& cache) const noexcept
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
    // �ȍ~�� THREAD_SAFE() �}�N���ɂ��C�����K�v
    // --> report() �̎������ɂ͓��l�̃}�N�����`���邩�Astd::lock_guard ���g�p����

    virtual void report(CALLER_ARG FILE* fp) = 0;

    int deleteByTime(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept
    {
        THREAD_SAFE();

        const auto OldAccessTime = [&threshold](const auto& it)
        {
            return it->second.mCreateTime < threshold;
        };

        const int delPositive = deleteBy(OldAccessTime, mPositive);
        const int delNegative = deleteBy(OldAccessTime, mNegative);

        const int sum = delPositive + delNegative;

        if (sum > 0)
        {
            NEW_LOG_BLOCK();

            traceW(L"* delete records: Positive=%d, Negative=%d", delPositive, delNegative);
        }

        return sum;
    }

    int deleteByKey(CALLER_ARG const WCSE::ObjectKey& argObjKey) noexcept
    {
        THREAD_SAFE();

        const auto EqualObjKey = [&argObjKey](const auto& it)
        {
            return it->first == argObjKey;
        };

        // �����ƈ�v������̂��L���b�V������폜

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
                return it->first == parentDir;
            };

            // �����̐e�f�B���N�g�����L���b�V������폜

            delPositiveP = deleteBy(EqualParentDir, mPositive);
            delNegativeP = deleteBy(EqualParentDir, mNegative);

            //traceW(L"delete records: PositiveP=%d NegativeP=%d", delPositiveP, delNegativeP);
        }
        else
        {
            // ������ "\bucket" �̏ꍇ�͂������ʉ߂��邪
            // �e�f�B���N�g���͑��݂��Ȃ��̂Ŗ��Ȃ�
        }

        const int sum = delPositive + delNegative + delPositiveP + delNegativeP;

        if (sum > 0)
        {
            NEW_LOG_BLOCK();

            traceW(L"* delete records: argObjKey=%s, Positive=%d, Negative=%d, PositiveP=%d, NegativeP=%d",
                argObjKey.c_str(), delPositive, delNegative, delPositiveP, delNegativeP);
        }

        return sum;
    }

    void clear(CALLER_ARG0) noexcept
    {
        THREAD_SAFE();

        mPositive.clear();
        mNegative.clear();
    }

    // ----------------------- Positive

    bool get(CALLER_ARG const WCSE::ObjectKey& argObjKey, T* pV) noexcept
    {
        THREAD_SAFE();
        APP_ASSERT(argObjKey.valid());

        const auto it{ mPositive.find(argObjKey) };

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

    void set(CALLER_ARG const WCSE::ObjectKey& argObjKey, const T& argV) noexcept
    {
        THREAD_SAFE();
        NEW_LOG_BLOCK();
        APP_ASSERT(argObjKey.valid());

        // �L���b�V���ɃR�s�[

        if (mPositive.find(argObjKey) == mPositive.end())
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

    bool isNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey) noexcept
    {
        THREAD_SAFE();
        APP_ASSERT(argObjKey.valid());

        auto it{ mNegative.find(argObjKey) };

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

    void addNegative(CALLER_ARG const WCSE::ObjectKey& argObjKey) noexcept
    {
        THREAD_SAFE();
        NEW_LOG_BLOCK();
        APP_ASSERT(argObjKey.valid());

        if (mNegative.find(argObjKey) == mNegative.end())
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

#undef THREAD_SAFE

#pragma warning(pop)

class CacheHeadObject : public ObjectCacheTmpl<WCSE::DirInfoType>
{
public:
    void report(CALLER_ARG FILE* fp);
};

class CacheListObjects : public ObjectCacheTmpl<WCSE::DirInfoListType>
{
public:
    void report(CALLER_ARG FILE* fp);
};

// EOF