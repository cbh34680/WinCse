#pragma once

#define USE_UNORDERED_MAP		(0)

#if USE_UNORDERED_MAP
#include <unordered_map>
#else
#include <map>
#endif

#include <set>
#include <chrono>
#include <functional>
#include "Purpose.h"


// S3 オブジェクト・キャッシュのキー
struct ObjectCacheKey
{
	Purpose mPurpose = Purpose::None;
	std::wstring mBucket;
	std::wstring mKey;

	ObjectCacheKey() = default;

	ObjectCacheKey(const Purpose argPurpose,
		const std::wstring& argBucket, const std::wstring& argKey)
		: mPurpose(argPurpose), mBucket(argBucket), mKey(argKey)
	{
	}

	ObjectCacheKey(const ObjectCacheKey& other)
	{
		mPurpose = other.mPurpose;
		mBucket = other.mBucket;
		mKey = other.mKey;
	}

#if USE_UNORDERED_MAP
	// unorderd_map のキーになるために必要
	bool operator==(const ObjectCacheKey& other) const
	{
		return mBucket == other.mBucket && mKey == other.mKey && mPurpose == other.mPurpose;
	}

#else
	//
	// std::map のキーにする場合に必要
	//
	bool operator<(const ObjectCacheKey& other) const
	{
		if (mBucket < other.mBucket) {			// bucket
			return true;
		}
		else if (mBucket > other.mBucket) {
			return false;
		}
		else if (mKey < other.mKey) {				// key
			return true;
		}
		else if (mKey > other.mKey) {
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
#endif
};

#if USE_UNORDERED_MAP
// カスタムハッシュ関数 ... unorderd_map のキーになるために必要
namespace std
{
	template <>
	struct hash<ObjectCacheKey>
	{
		size_t operator()(const ObjectCacheKey& that) const
		{
			return hash<wstring>()(that.mBucket) ^ (hash<wstring>()(that.mKey) << 1) ^ (hash<int>()(that.mPurpose) << 2);
		}
	};
}
#endif

struct NegativeCacheVal
{
	std::wstring mCreateCallChain;
	std::wstring mAccessCallChain;
	std::chrono::system_clock::time_point mCreateTime;
	std::chrono::system_clock::time_point mAccessTime;
	int mRefCount = 0;

	NegativeCacheVal(CALLER_ARG0)
	{
		mCreateCallChain = mAccessCallChain = CALL_CHAIN();
		mCreateTime = mAccessTime = std::chrono::system_clock::now();
	}
};

struct PosisiveCacheVal : public NegativeCacheVal
{
	DirInfoListType mDirInfoList;

	PosisiveCacheVal(CALLER_ARG
		const DirInfoListType& argDirInfoList)
		: NegativeCacheVal(CONT_CALLER0), mDirInfoList(argDirInfoList)
	{
	}
};

class ObjectCache
{
private:
#if USE_UNORDERED_MAP
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
	void report(CALLER_ARG FILE* fp);

	int deleteOldRecords(CALLER_ARG std::chrono::system_clock::time_point threshold);

	bool getPositive(CALLER_ARG const Purpose argPurpose,
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoListType* pDirInfoList);

	void setPositive(CALLER_ARG const Purpose argPurpose,
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoListType& pDirInfoList);

	bool getPositive_File(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoType* pDirInfo);

	void setPositive_File(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoType& pDirInfo);

	bool isInNegative(CALLER_ARG const Purpose argPurpose,
		const std::wstring& argBucket, const std::wstring& argKey);

	void addNegative(CALLER_ARG const Purpose argPurpose,
		const std::wstring& argBucket, const std::wstring& argKey);

	bool isInNegative_File(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey);

	void addNegative_File(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey);
};

// EOF