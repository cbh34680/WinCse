#pragma once

#include "WinCseLib.h"
#include "aws_sdk_s3.h"

#include <regex>
#include "Protect.hpp"

#include "ListBucketsCache.hpp"
#include "HeadObjectCache.hpp"
#include "ListObjectsCache.hpp"

struct FileOutputParams
{
	std::wstring mPath;
	DWORD mCreationDisposition;
	bool mSpecifyRange = false;
	UINT64 mOffset = 0;
	ULONG mLength = 0;

	FileOutputParams(
		std::wstring argPath, DWORD argCreationDisposition,
		UINT64 argOffset, ULONG argLength)
		:
		mPath(argPath), mCreationDisposition(argCreationDisposition),
		mSpecifyRange(true), mOffset(argOffset), mLength(argLength)
	{
	}

	FileOutputParams(std::wstring argPath, DWORD argCreationDisposition)
		:
		mPath(argPath), mCreationDisposition(argCreationDisposition)
	{
	}

	UINT64 getOffsetEnd() const
	{
		APP_ASSERT(mLength > 0);
		return mOffset + mLength - 1;
	}

	std::wstring str() const;
};

class ClientPtr : public std::unique_ptr<Aws::S3::S3Client>
{
	// 本来は std::atomic<int> だが、ただの参照値なので厳密でなくても OK
	// operator=() の実装を省略 :-)
	//std::atomic<int> mRefCount = 0;
	int mRefCount = 0;

public:
	using std::unique_ptr<Aws::S3::S3Client>::unique_ptr;

	Aws::S3::S3Client* operator->() noexcept
	{
		mRefCount++;

		return std::unique_ptr<Aws::S3::S3Client>::operator->();
	}

	int getRefCount() const { return mRefCount; }
};

//
// open() が呼ばれたときに UParam として PTFS_FILE_CONTEXT に保存する内部情報
// close() で削除される
//
struct OpenContext : public WCSE::CSDeviceContext
{
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;

	OpenContext(
		const std::wstring& argCacheDataDir,
		const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const UINT32 argCreateOptions,
		const UINT32 argGrantedAccess)
		:
		CSDeviceContext(argCacheDataDir, argObjKey, argFileInfo),
		mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess)
	{
	}

	NTSTATUS openFileHandle(CALLER_ARG DWORD argDesiredAccess, DWORD argCreationDisposition);
};

class AwsS3 : public WCSE::ICSDevice
{
private:
	struct
	{
		int maxDisplayBuckets;
		int maxDisplayObjects;
		bool deleteAfterUpload;
		int bucketCacheExpiryMin;
		int objectCacheExpiryMin;
		int cacheFileRetentionMin;
		bool strictFileTimestamp;
	}
	mConfig = {};

	FSP_FILE_SYSTEM* mFileSystem = nullptr;

	ListBucketsCache mListBucketsCache;
	HeadObjectCache mHeadObjectCache;
	ListObjectsCache mListObjectsCache;

	FSP_SERVICE* mWinFspService = nullptr;
	WINCSE_DEVICE_STATS* mStats = nullptr;
	WINCSE_DEVICE_STATS mStats_{};

	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::wstring mWorkDir;
	std::wstring mCacheDataDir;
	std::wstring mCacheReportDir;

	UINT64 mWorkDirCTime = 0;
	std::wstring mRegion;
	//FSP_FILE_SYSTEM* mFileSystem = nullptr;

	UINT32 mDefaultFileAttributes = 0;

	// 属性参照用ファイル・ハンドル
	WCSE::FileHandle mRefFile;
	WCSE::FileHandle mRefDir;

	// ファイル生成時の排他制御
	struct PrepareLocalFileShare : public SharedBase { };
	ShareStore<PrepareLocalFileShare> mPrepareLocalFileShare;

	// シャットダウン要否判定のためポインタ
	std::unique_ptr<Aws::SDKOptions> mSDKOptions;

	// S3 クライアント
	ClientPtr mClient;

	// Worker 取得
	const std::unordered_map<std::wstring, WCSE::IWorker*> mWorkers;

	WCSE::IWorker* getWorker(const std::wstring& argName)
	{
		return mWorkers.at(argName);
	}

	void addTasks(CALLER_ARG0);
	bool setupNotifListener(CALLER_ARG0);
	void notifListener();

	// バケット名フィルタ
	std::vector<std::wregex> mBucketFilters;
	bool isInBucketFilters(const std::wstring& arg);

	// バケット操作関連
	void clearListBucketsCache(CALLER_ARG0);
	bool reloadListBucketsCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	void reportListBucketsCache(CALLER_ARG FILE* fp);
	std::wstring getBucketLocation(CALLER_ARG const std::wstring& bucketName);

	// AWS SDK API を実行
	WCSE::DirInfoType apicallHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool apicallListObjectsV2(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		bool argDelimiter, int argLimit, WCSE::DirInfoListType* pDirInfoList);

	// Read 関連

	NTSTATUS prepareLocalFile_simple(CALLER_ARG OpenContext* ctx, UINT64 argOffset, ULONG argLength);
	bool doMultipartDownload(CALLER_ARG OpenContext* ctx, const std::wstring& localPath);

	bool unsafeHeadBucket(CALLER_ARG const std::wstring& bucketName,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);
	bool unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */,
		const std::vector<std::wstring>& options);

	// list 関連
	WCSE::DirInfoType unsafeHeadObjectWithCache(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	WCSE::DirInfoType unsafeHeadObjectWithCache_CheckDir(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool unsafeListObjectsWithCache(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList /* nullable */);

	WCSE::DirInfoType getCachedHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool isNegativeHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);

	void reportObjectCache(CALLER_ARG FILE* fp);
	int deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	int clearObjectCache(CALLER_ARG0);
	int deleteObjectCache(CALLER_ARG const WCSE::ObjectKey& argObjKey);

public:
	// 外部から呼び出されるため override ではないが public のメソッド

	void onTimer(CALLER_ARG0);
	void onIdle(CALLER_ARG0);

	INT64 getObjectAndWriteToFile(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FileOutputParams& argOutputParams);

public:
	AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection,
		std::unordered_map<std::wstring, WCSE::IWorker*>&& argWorkers);

	~AwsS3();

	void queryStats(WINCSE_DEVICE_STATS* pStats) override
	{
		*pStats = *mStats;
	}

	NTSTATUS PreCreateFilesystem(FSP_SERVICE *Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;
	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;
	bool listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */) override;

	bool headObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;

	bool listObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList /* nullable */) override;

	bool listDisplayObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList) override;

	bool deleteObject(CALLER_ARG const WCSE::ObjectKey& argObjKey) override;

	bool putObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const std::wstring& argFilePath) override;

	bool renameObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		const std::wstring& argFileName, const std::wstring& argNewFileName, BOOLEAN argReplaceIfExists) override;

	WCSE::CSDeviceContext* create(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes) override;

	WCSE::CSDeviceContext* open(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		UINT32 CreateOptions, UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo) override;

	void close(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext) override;

	NTSTATUS readObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	NTSTATUS writeObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS getHandleFromContext(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		DWORD argDesiredAccess, DWORD argCreationDisposition, PHANDLE pHandle) override;

private:
	// ファイル/ディレクトリに特化
	WCSE::DirInfoType makeDirInfo_attr(const std::wstring& argFileName, UINT64 argFileTime, UINT32 argFileAttributes);
	WCSE::DirInfoType makeDirInfo_byName(const std::wstring& argFileName, UINT64 argFileTime);
	WCSE::DirInfoType makeDirInfo_dir(const std::wstring& argFileName, UINT64 argFileTime);
};

template<typename T>
bool outcomeIsSuccess(const T& outcome)
{
	const bool suc = outcome.IsSuccess();
	if (!suc)
	{
		NEW_LOG_BLOCK();

		traceA("outcome.IsSuccess()=%s: %s", suc ? "true" : "false", typeid(outcome).name());

		const auto& err{ outcome.GetError() };
		const auto mesg{ err.GetMessage().c_str() };
		const auto code{ err.GetResponseCode() };
		const auto type{ err.GetErrorType() };
		const auto name{ err.GetExceptionName().c_str() };

		traceA("error: type=%d, code=%d, name=%s, message=%s", type, code, name, mesg);
	}

	return suc;
}

#ifdef WINCSEAWSS3_EXPORTS
#define AWSS3_API __declspec(dllexport)
#else
#define AWSS3_API __declspec(dllimport)
#endif

extern "C"
{
	AWSS3_API WCSE::ICSDevice* NewCSDevice(PCWSTR argTempDir, PCWSTR argIniSection, WCSE::NamedWorker argWorkers[]);
}

#define StatsIncr(name)				::InterlockedIncrement(& (this->mStats->name))

#define AWS_DEFAULT_REGION			Aws::Region::US_EAST_1

// EOF