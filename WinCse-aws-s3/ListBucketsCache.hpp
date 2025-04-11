#pragma once

class ListBucketsCache
{
private:
	WCSE::DirInfoListType mList;
	std::unordered_map<std::wstring, std::wstring> mBucketRegions;

	std::wstring mLastSetCallChain;
	std::wstring mLastGetCallChain;
	std::wstring mLastClearCallChain;

	std::chrono::system_clock::time_point mLastSetTime;
	std::chrono::system_clock::time_point mLastGetTime;
	std::chrono::system_clock::time_point mLastClearTime;

	unsigned mCountGet = 0;
	unsigned mCountSet = 0;
	unsigned mCountClear = 0;

	unsigned mCountGetNegative = 0;
	unsigned mCountSetNegative = 0;

protected:
public:
	std::chrono::system_clock::time_point getLastSetTime(CALLER_ARG0) const noexcept;

	bool getBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion) const noexcept;
	void addBucketRegion(CALLER_ARG const std::wstring& argBucketName, const std::wstring& argBucketRegion) noexcept;

	void clear(CALLER_ARG0) noexcept;

	bool empty(CALLER_ARG0) const noexcept
	{
		return mList.empty();
	}

	void set(CALLER_ARG const WCSE::DirInfoListType& argDirInfoList) noexcept;
	WCSE::DirInfoListType get(CALLER_ARG0) noexcept;
	WCSE::DirInfoType find(CALLER_ARG const std::wstring& argBucketName) noexcept;

	void report(CALLER_ARG FILE* fp) const noexcept;
};

// EOF