#pragma once

#include "CSDeviceCommon.h"

namespace CSEDAS3
{

class CacheListBuckets final
{
private:
	CSELIB::DirInfoPtrList							mList;
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
	std::chrono::system_clock::time_point clbGetLastSetTime(CALLER_ARG0) const noexcept;
	void clbClear(CALLER_ARG0) noexcept;
	bool clbEmpty(CALLER_ARG0) const noexcept;
	void clbSet(CALLER_ARG const CSELIB::DirInfoPtrList& argDirInfoList) noexcept;
	void clbGet(CALLER_ARG CSELIB::DirInfoPtrList* pDirInfoList) const noexcept;
	bool clbFind(CALLER_ARG const std::wstring& argBucketName, CSELIB::DirInfoPtr* pDirInfo) const noexcept;
	bool clbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion) const noexcept;
	void clbAddBucketRegion(CALLER_ARG const std::wstring& argBucketName, const std::wstring& argBucketRegion) noexcept;
	void clbReport(CALLER_ARG FILE* fp) const noexcept;
};

}	// namespace CSEDAS3

// EOF