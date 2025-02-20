#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <memory>
#include <functional>

// S3 オブジェクト・キャッシュのキー
struct ObjectCacheKey
{
	std::wstring bucket;
	std::wstring key;
	int limit;
	bool delimiter;

	ObjectCacheKey() : limit(0), delimiter(false) { }

	ObjectCacheKey(const std::wstring& argBucket,
		const std::wstring& argKey, const int argLimit, const bool argDelimiter)
		: bucket(argBucket), key(argKey), limit(argLimit), delimiter(argDelimiter) { }

	ObjectCacheKey(const ObjectCacheKey& other)
	{
		bucket = other.bucket;
		key = other.key;
		limit = other.limit;
		delimiter = other.delimiter;
	}

	bool operator<(const ObjectCacheKey& other) const
	{
		if (bucket < other.bucket) {			// bucket
			return true;
		}
		else if (bucket > other.bucket) {
			return false;
		}
		else if (key < other.key) {				// key
			return true;
		}
		else if (key > other.key) {
			return false;
		}
		else if (limit < other.limit) {			// limit
			return true;
		}
		else if (limit > other.limit) {
			return false;
		}
		else if (delimiter < other.delimiter) {	// delimiter
			return true;
		}
		else if (delimiter > other.delimiter) {
			return false;
		}

		return false;
	}
};

struct NegativeCacheVal
{
	std::wstring lastCallChain;
	std::chrono::system_clock::time_point lastAccessTime;
	int refCount = 0;

	NegativeCacheVal(CALLER_ARG0)
		: lastCallChain(CALL_CHAIN())
	{
		lastAccessTime = std::chrono::system_clock::now();
	}
};

struct PosisiveCacheVal : public NegativeCacheVal
{
	std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

	PosisiveCacheVal(CALLER_ARG
		const std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>& argDirInfoList)
		: NegativeCacheVal(CONT_CALLER0), dirInfoList(argDirInfoList)
	{
	}
};

class ObjectCache
{
private:
	std::map<ObjectCacheKey, PosisiveCacheVal> mPositive;
	std::map<ObjectCacheKey, NegativeCacheVal> mNegative;
	int mGetPositive = 0;
	int mSetPositive = 0;
	int mGetNegative = 0;
	int mSetNegative = 0;

protected:
public:
	void report(CALLER_ARG0);

	int deleteOldRecords(CALLER_ARG std::chrono::system_clock::time_point threshold);

	bool getPositive(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey, const int limit, const bool delimiter,
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* dirInfoList);

	void setPositive(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey, const int limit, const bool delimiter,
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>& dirInfoList);

	bool isInNegative(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey, const int limit, const bool delimiter);

	void addNegative(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey, const int limit, const bool delimiter);
};

// EOF