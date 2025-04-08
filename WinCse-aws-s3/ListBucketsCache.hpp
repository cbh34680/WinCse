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
	std::chrono::system_clock::time_point getLastSetTime(CALLER_ARG0) const;

	std::wstring getBucketRegion(CALLER_ARG const std::wstring& argBucketName);
	void addBucketRegion(CALLER_ARG const std::wstring& argBucketName, const std::wstring& argRegion);

	void clear(CALLER_ARG0);

	bool empty(CALLER_ARG0)
	{
		return mList.empty();
	}

	void set(CALLER_ARG const WCSE::DirInfoListType& argDirInfoList);
	WCSE::DirInfoListType get(CALLER_ARG0);
	WCSE::DirInfoType find(CALLER_ARG const std::wstring& argBucketName);

	void report(CALLER_ARG FILE* fp);
};

// EOF