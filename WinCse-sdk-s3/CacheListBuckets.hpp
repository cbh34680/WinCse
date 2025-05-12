#pragma once

#include "SdkS3Common.h"

namespace CSESS3
{

class CacheListBuckets final
{
private:
	CSELIB::DirEntryListType						mList;
	std::map<std::wstring, std::wstring>			mBucketRegions;

	mutable std::mutex								mGuard;
	mutable std::wstring							mLastSetCallChain;
	mutable std::wstring							mLastGetCallChain;
	mutable std::wstring							mLastClearCallChain;
	mutable std::chrono::system_clock::time_point	mLastSetTime;
	mutable std::chrono::system_clock::time_point	mLastGetTime;
	mutable std::chrono::system_clock::time_point	mLastClearTime;
	mutable int										mCountGet = 0;
	mutable int										mCountSet = 0;
	mutable int										mCountClear = 0;
	mutable int										mCountGetNegative = 0;
	mutable int										mCountSetNegative = 0;

public:
	WINCSESDKS3_API std::chrono::system_clock::time_point clbGetLastSetTime(CALLER_ARG0) const;
	WINCSESDKS3_API void clbClear(CALLER_ARG0);
	WINCSESDKS3_API bool clbEmpty(CALLER_ARG0) const;
	WINCSESDKS3_API void clbSet(CALLER_ARG const CSELIB::DirEntryListType& argDirEntryList);
	WINCSESDKS3_API void clbGet(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) const;
	WINCSESDKS3_API bool clbFind(CALLER_ARG const std::wstring& argBucketName, CSELIB::DirEntryType* pDirEntry) const;
	WINCSESDKS3_API bool clbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion) const;
	WINCSESDKS3_API void clbAddBucketRegion(CALLER_ARG const std::wstring& argBucketName, const std::wstring& argBucketRegion);
	WINCSESDKS3_API void clbReport(CALLER_ARG FILE* fp) const;
};

}	// namespace CSESS3

// EOF