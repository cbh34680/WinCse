#pragma once

#include "WinCseLib.h"

class AwsS3B : public WCSE::ICSDevice
{
private:
	bool setupNotifListener(CALLER_ARG0);
	void notifListener();

protected:
	struct Settings
	{
		explicit Settings(
			int argMaxDisplayBuckets,
			int argMaxDisplayObjects,
			bool argDeleteAfterUpload,
			int argBucketCacheExpiryMin,
			int argObjectCacheExpiryMin,
			int argCacheFileRetentionMin,
			bool argStrictFileTimestamp)
			:
			maxDisplayBuckets(argMaxDisplayBuckets),
			maxDisplayObjects(argMaxDisplayObjects),
			deleteAfterUpload(argDeleteAfterUpload),
			bucketCacheExpiryMin(argBucketCacheExpiryMin),
			objectCacheExpiryMin(argObjectCacheExpiryMin),
			cacheFileRetentionMin(argCacheFileRetentionMin),
			strictFileTimestamp(argStrictFileTimestamp)
		{
		}

		const int maxDisplayBuckets;
		const int maxDisplayObjects;
		const bool deleteAfterUpload;
		const int bucketCacheExpiryMin;
		const int objectCacheExpiryMin;
		const int cacheFileRetentionMin;
		const bool strictFileTimestamp;
	};

	std::unique_ptr<Settings> mSettings;

	const std::wstring mTempDir;
	const std::wstring mIniSection;

	const std::unordered_map<std::wstring, WCSE::IWorker*> mWorkers;

	WINCSE_DEVICE_STATS* mStats = nullptr;
	WINCSE_DEVICE_STATS mStats_{};

	FSP_SERVICE* mWinFspService = nullptr;
	FSP_FILE_SYSTEM* mFileSystem = nullptr;

	UINT32 mDefaultFileAttributes = 0;

	// 属性参照用ファイル・ハンドル
	WCSE::FileHandle mRefFile;
	WCSE::FileHandle mRefDir;

	std::wstring mWorkDir;
	UINT64 mWorkDirCTime = 0;
	std::wstring mConfPath;

	void queryStats(WINCSE_DEVICE_STATS* pStats) override
	{
		*pStats = *mStats;
	}

	WCSE::IWorker* getWorker(const std::wstring& argName)
	{
		return mWorkers.at(argName);
	}

	virtual void onNotifEvent(CALLER_ARG [[maybe_unused]] DWORD argEventId, [[maybe_unused]] PCWSTR argEventName) { }

	// ファイル/ディレクトリに特化
	WCSE::DirInfoType makeDirInfo_attr(const std::wstring& argFileName, UINT64 argFileTime, UINT32 argFileAttributes);
	WCSE::DirInfoType makeDirInfo_byName(const std::wstring& argFileName, UINT64 argFileTime);
	WCSE::DirInfoType makeDirInfo_dir(const std::wstring& argFileName, UINT64 argFileTime);

public:
	explicit AwsS3B(const std::wstring& argTempDir, const std::wstring& argIniSection,
		std::unordered_map<std::wstring, WCSE::IWorker*>&& argWorkers);

	virtual ~AwsS3B() noexcept = default;

	virtual void onTimer(CALLER_ARG0) { }
	virtual void onIdle(CALLER_ARG0) { }

	//
	NTSTATUS PreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;
};

#define StatsIncr(name)				::InterlockedIncrement(& (this->mStats->name))

// EOF