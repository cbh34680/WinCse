#pragma once

class BucketCache
{
private:
	DirInfoListType mList;
	std::wstring mLastSetCallChain;
	std::wstring mLastGetCallChain;
	std::chrono::system_clock::time_point mLastSetTime;
	std::chrono::system_clock::time_point mLastGetTime;
	unsigned mCountGet = 0;
	unsigned mCountSet = 0;

	std::unordered_map<std::wstring, std::wstring> mRegionMap;

protected:
public:
	std::chrono::system_clock::time_point getLastSetTime(CALLER_ARG0) const;

	bool findRegion(CALLER_ARG const std::wstring& bucketName, std::wstring* bucketRegion);

	void updateRegion(CALLER_ARG const std::wstring& bucketName, const std::wstring& bucketRegion);

	void clear(CALLER_ARG0)
	{
		mList.clear();
	}

	bool empty(CALLER_ARG0)
	{
		return mList.empty();
	}

	void save(CALLER_ARG
		const DirInfoListType& dirInfoList);

	void load(CALLER_ARG const std::wstring& region,
		DirInfoListType& dirInfoList);

	DirInfoType find(CALLER_ARG const std::wstring& argBucket);

	void report(CALLER_ARG FILE* fp);

};

// EOF