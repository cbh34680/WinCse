#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>

class BucketCache
{
private:
	std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> mList;
	std::wstring mLastSetCallChain;
	std::wstring mLastGetCallChain;
	std::chrono::system_clock::time_point mLastSetTime;
	std::chrono::system_clock::time_point mLastGetTime;
	unsigned mCountGet = 0;
	unsigned mCountSet = 0;

	std::map<std::wstring, std::wstring> mRegionMap;

protected:
public:
	std::chrono::system_clock::time_point getLastSetTime(CALLER_ARG0) const;

	bool findRegion(CALLER_ARG const std::wstring& bucketName, std::wstring* bucketRegion);

	void updateRegion(CALLER_ARG const std::wstring& bucketName, const std::wstring& bucketRegion);

	void clear(CALLER_ARG0);

	bool empty(CALLER_ARG0);

	void save(CALLER_ARG
		const std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>& dirInfoList);

	void load(CALLER_ARG const std::wstring& region,
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>& dirInfoList);

	std::shared_ptr<FSP_FSCTL_DIR_INFO> find(CALLER_ARG const std::wstring& argBucket);

	void report(CALLER_ARG0);

};

// EOF