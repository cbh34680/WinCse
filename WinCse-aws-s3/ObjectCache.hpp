#pragma once

#define OBJECT_CACHE_KEY_UNORDERED_MAP		(0)

#if !OBJECT_CACHE_KEY_UNORDERED_MAP
#include <map>
#endif

#include <set>
#include <chrono>
#include <functional>
#include "Purpose.h"


// S3 オブジェクト・キャッシュのキー
struct ObjectCacheKey
{
	WinCseLib::ObjectKey mObjKey;
	Purpose mPurpose = Purpose::None;

	ObjectCacheKey(const WinCseLib::ObjectKey& argObjectKey, const Purpose argPurpose)
		: mObjKey(argObjectKey), mPurpose(argPurpose)
	{
	}

	//
	// unorderd_map のキーになるために必要
	//
	bool operator==(const ObjectCacheKey& other) const
	{
		return mObjKey == other.mObjKey && mPurpose == other.mPurpose;
	}

	//
	// std::map のキーにする場合に必要
	//
	bool operator<(const ObjectCacheKey& other) const
	{
		if (mObjKey < other.mObjKey) {		// ObjectKey
			return true;
		}
		else if (mObjKey > other.mObjKey) {
			return false;
		}
		else if (mPurpose < other.mPurpose) {		// purpose
			return true;
		}
		else if (mPurpose > other.mPurpose) {
			return false;
		}

		return false;
	}
};

// カスタムハッシュ関数 ... unorderd_map のキーになるために必要
namespace std
{
	template <>
	struct hash<ObjectCacheKey>
	{
		size_t operator()(const ObjectCacheKey& that) const
		{
			return hash<WinCseLib::ObjectKey>()(that.mObjKey) ^ (hash<int>()(static_cast<int>(that.mPurpose)) << 2);
		}
	};
}

struct BaseCacheVal
{
	std::wstring mCreateCallChain;
	std::wstring mAccessCallChain;
	std::chrono::system_clock::time_point mCreateTime;
	std::chrono::system_clock::time_point mAccessTime;
	int mRefCount = 0;

	BaseCacheVal(CALLER_ARG0)
	{
		mCreateCallChain = mAccessCallChain = CALL_CHAIN();
		mCreateTime = mAccessTime = std::chrono::system_clock::now();
	}
};

struct NegativeCacheVal : public BaseCacheVal { };

struct PosisiveCacheVal : public BaseCacheVal
{
	DirInfoListType mDirInfoList;

	PosisiveCacheVal(CALLER_ARG
		const DirInfoListType& argDirInfoList)
		: BaseCacheVal(CONT_CALLER0), mDirInfoList(argDirInfoList)
	{
	}
};

class ObjectCache
{
private:
#if OBJECT_CACHE_KEY_UNORDERED_MAP
	std::unordered_map<ObjectCacheKey, PosisiveCacheVal> mPositive;
	std::unordered_map<ObjectCacheKey, NegativeCacheVal> mNegative;
#else
	std::map<ObjectCacheKey, PosisiveCacheVal> mPositive;
	std::map<ObjectCacheKey, NegativeCacheVal> mNegative;
#endif

	int mGetPositive = 0;
	int mSetPositive = 0;
	int mUpdPositive = 0;

	int mGetNegative = 0;
	int mSetNegative = 0;
	int mUpdNegative = 0;

protected:
public:
	void clear(CALLER_ARG0)
	{
		mPositive.clear();
		mNegative.clear();
	}

	void report(CALLER_ARG FILE* fp);

	bool getPositive(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const Purpose argPurpose, DirInfoListType* pDirInfoList);
	void setPositive(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		Purpose argPurpose, DirInfoListType& pDirInfoList);
	bool getPositive_File(CALLER_ARG
		const WinCseLib::ObjectKey& argObjKey, DirInfoType* pDirInfo);
	void setPositive_File(CALLER_ARG
		const WinCseLib::ObjectKey& argObjKey, DirInfoType& pDirInfo);
	bool isInNegative(CALLER_ARG const WinCseLib::ObjectKey& argObjKey, const Purpose argPurpose);
	void addNegative(CALLER_ARG const WinCseLib::ObjectKey& argObjKey, const Purpose argPurpose);
	bool isInNegative_File(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);
	void addNegative_File(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

	int deleteOldRecords(CALLER_ARG std::chrono::system_clock::time_point threshold);

	int deleteByObjectKey(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);
};

// EOF