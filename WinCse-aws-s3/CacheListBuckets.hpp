#pragma once

#include "WinCseLib.h"

class CacheListBuckets
{
private:
	WCSE::DirInfoListType mList;
	std::unordered_map<std::wstring, std::wstring> mBucketRegions;

	mutable std::wstring mLastSetCallChain;
	mutable std::wstring mLastGetCallChain;
	mutable std::wstring mLastClearCallChain;

	mutable std::chrono::system_clock::time_point mLastSetTime;
	mutable std::chrono::system_clock::time_point mLastGetTime;
	mutable std::chrono::system_clock::time_point mLastClearTime;

	mutable unsigned mCountGet = 0;
	mutable unsigned mCountSet = 0;
	mutable unsigned mCountClear = 0;

	mutable unsigned mCountGetNegative = 0;
	mutable unsigned mCountSetNegative = 0;

protected:
public:
	std::chrono::system_clock::time_point getLastSetTime(CALLER_ARG0) const noexcept;

	void clear(CALLER_ARG0) noexcept;

	bool empty(CALLER_ARG0) const noexcept;

	void set(CALLER_ARG const WCSE::DirInfoListType& argDirInfoList) noexcept;

	void get(CALLER_ARG WCSE::DirInfoListType* pDirInfoList) const noexcept;

	bool find(CALLER_ARG const std::wstring& argBucketName, WCSE::DirInfoType* pDirInfo) const noexcept;

	bool getBucketRegion(CALLER_ARG
		const std::wstring& argBucketName, std::wstring* pBucketRegion) const noexcept;

	void addBucketRegion(CALLER_ARG
		const std::wstring& argBucketName, const std::wstring& argBucketRegion) noexcept;

	void report(CALLER_ARG FILE* fp) const noexcept;
};

// EOF