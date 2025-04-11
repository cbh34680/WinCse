#pragma once

#include "AwsS3A.hpp"
#include "ListBucketsCache.hpp"
#include "ObjectCache.hpp"

class AwsS3C : public AwsS3A
{
private:
	std::wstring mCacheReportDir;

	ListBucketsCache mListBucketsCache;
	HeadObjectCache mHeadObjectCache;
	ListObjectsCache mListObjectsCache;

protected:
	std::wstring mCacheDataDir;

	// bucket
	void clearListBucketsCache(CALLER_ARG0);
	void reportListBucketsCache(CALLER_ARG FILE* fp);

	// object
	WCSE::DirInfoType getCachedHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool isNegativeHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	void reportObjectCache(CALLER_ARG FILE* fp);
	int deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	int clearObjectCache(CALLER_ARG0);
	int deleteObjectCache(CALLER_ARG const WCSE::ObjectKey& argObjKey);

	// unsafe bucket
	std::wstring unsafeGetBucketRegion(CALLER_ARG const std::wstring& argBucketName);
	bool unsafeHeadBucket(CALLER_ARG const std::wstring& bucketName,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);
	bool unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */,
		const std::vector<std::wstring>& options);
	bool unsafeReloadListBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold);

	// unsafe object
	WCSE::DirInfoType unsafeHeadObjectWithCache(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	WCSE::DirInfoType unsafeHeadObjectWithCache_CheckDir(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool unsafeListObjectsWithCache(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList /* nullable */);

protected:
	void onNotifEvent(CALLER_ARG DWORD argEventId, PCWSTR argEventName) override;

public:
	void onTimer(CALLER_ARG0) override;
	void onIdle(CALLER_ARG0) override;

public:
	using AwsS3A::AwsS3A;

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;
};

// EOF