#pragma once

#include "WinCseLib.h"
#include "aws_sdk_s3.h"

#include <regex>
#include "Purpose.h"
#include "Protect.hpp"

struct FileOutputParams
{
	std::wstring mPath;
	DWORD mCreationDisposition;
	bool mSpecifyRange;
	UINT64 mOffset;
	ULONG mLength;

	FileOutputParams(
		std::wstring argPath, DWORD argCreationDisposition,
		bool argSpecifyRange, UINT64 argOffset, ULONG argLength)
		:
		mPath(argPath), mCreationDisposition(argCreationDisposition),
		mSpecifyRange(argSpecifyRange), mOffset(argOffset), mLength(argLength)
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
struct OpenContext : public WinCseLib::CSDeviceContext
{
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;

	OpenContext(
		const std::wstring& argCacheDataDir,
		const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const UINT32 argCreateOptions,
		const UINT32 argGrantedAccess)
		:
		CSDeviceContext(argCacheDataDir, argObjKey, argFileInfo),
		mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess)
	{
	}

	NTSTATUS openFileHandle(CALLER_ARG const DWORD argDesiredAccess, const DWORD argCreationDisposition);
};

class AwsS3 : public WinCseLib::ICSDevice
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
	FSP_FILE_SYSTEM* mFileSystem = nullptr;

	UINT32 mDefaultFileAttributes = 0;

	// 属性参照用ファイル・ハンドル
	WinCseLib::FileHandle mRefFile;
	WinCseLib::FileHandle mRefDir;

	// ファイル生成時の排他制御
	struct PrepareLocalCacheFileShared : public SharedBase { };
	ShareStore<PrepareLocalCacheFileShared> mGuardPrepareLocalCache;

	// シャットダウン要否判定のためポインタ
	std::unique_ptr<Aws::SDKOptions> mSDKOptions;

	// S3 クライアント
	ClientPtr mClient;

	// Worker 取得
	const std::unordered_map<std::wstring, WinCseLib::IWorker*> mWorkers;

	WinCseLib::IWorker* getWorker(const std::wstring& argName)
	{
		return mWorkers.at(argName);
	}

	void addTasks(CALLER_ARG0);
	void notifListener();

	// バケット名フィルタ
	std::vector<std::wregex> mBucketFilters;
	bool isInBucketFilters(const std::wstring& arg);

	// バケット操作関連
	void clearBucketCache(CALLER_ARG0);
	bool reloadBucketCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	void reportBucketCache(CALLER_ARG FILE* fp);
	std::wstring unsafeGetBucketRegion(CALLER_ARG const std::wstring& bucketName);

	// オブジェクト操作関連
	void reportObjectCache(CALLER_ARG FILE* fp);
	int deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	int clearObjectCache(CALLER_ARG0);
	int deleteObjectCache(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

	//
	void unsafeReportObjectCache(CALLER_ARG FILE* fp);
	int unsafeDeleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	int unsafeClearObjectCache(CALLER_ARG0);
	int unsafeDeleteObjectCache(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

	// AWS SDK API を実行
	DirInfoType apicallHeadObject(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);
	bool apicallListObjectsV2(CALLER_ARG const Purpose purpose,
		const WinCseLib::ObjectKey& argObjKey, DirInfoListType* pDirInfoList);

	//
	bool unsafeHeadBucket(CALLER_ARG const std::wstring& bucketName,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);
	bool unsafeListBuckets(CALLER_ARG DirInfoListType* pDirInfoList /* nullable */,
		const std::vector<std::wstring>& options);

	//
	bool unsafeHeadObjectWithCache(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);
	bool unsafeListObjectsWithCache(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const Purpose purpose, DirInfoListType* pDirInfoList /* nullable */);
	bool unsafeGetPositiveCache_File(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		DirInfoType* pDirInfo);

	// Read 関連

	NTSTATUS prepareLocalFile(CALLER_ARG OpenContext* ctx);
	NTSTATUS prepareLocalFile_Simple(CALLER_ARG OpenContext* ctx);
	NTSTATUS prepareLocalFile_Multipart(CALLER_ARG OpenContext* ctx);
	bool doMultipartDownload(CALLER_ARG OpenContext* ctx, const std::wstring& localPath);

public:
	// 外部から呼び出されるため override ではないが public のメソッド

	void onTimer(CALLER_ARG0);
	void onIdle(CALLER_ARG0);

	int64_t getObjectAndWriteToFile(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const FileOutputParams& argOutputParams);

public:
	AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection,
		std::unordered_map<std::wstring, WinCseLib::IWorker*>&& argWorkers);

	~AwsS3();

	void queryStats(WINCSE_DEVICE_STATS* pStats) override
	{
		*pStats = *mStats;
	}

	bool PreCreateFilesystem(FSP_SERVICE *Service, const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;
	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;
	DirInfoType getBucket(CALLER_ARG const std::wstring& bucketName) override;
	bool listBuckets(CALLER_ARG DirInfoListType* pDirInfoList /* nullable */) override;

	bool headObject_File(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;
	bool headObject_Dir(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;

	bool listObjects(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		DirInfoListType* pDirInfoList /* nullable */) override;

	bool deleteObject(CALLER_ARG const WinCseLib::ObjectKey& argObjKey) override;

	bool putObject(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const wchar_t* sourceFile /* nullable */) override;

	WinCseLib::CSDeviceContext* create(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& fileInfo, const UINT32 CreateOptions,
		const UINT32 GrantedAccess, const UINT32 FileAttributes) override;

	WinCseLib::CSDeviceContext* open(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo) override;

	void close(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext) override;

	NTSTATUS readObject(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	NTSTATUS writeObject(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) override;

	NTSTATUS getHandleFromContext(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		const DWORD argDesiredAccess, const DWORD argCreationDisposition, PHANDLE pHandle) override;

private:
	// ファイル/ディレクトリに特化
	DirInfoType makeDirInfo_attr(const std::wstring& argFileName, const UINT64 argFileTime, const UINT32 argFileAttributes);
	DirInfoType makeDirInfo_byName(const std::wstring& argFileName, const UINT64 argFileTime);
	DirInfoType makeDirInfo_dir(const std::wstring& argFileName, const UINT64 argFileTime);
};

constexpr uint64_t PART_LENGTH_BYTE = WinCseLib::FILESIZE_1MiBu * 4;

NTSTATUS syncFileAttributes(CALLER_ARG
	const FSP_FSCTL_FILE_INFO& fileInfo, const std::wstring& localPath, bool* pNeedDownload);

template<typename T>
bool outcomeIsSuccess(const T& outcome)
{
	const bool suc = outcome.IsSuccess();
	if (!suc)
	{
		NEW_LOG_BLOCK();

		traceA("outcome.IsSuccess()=%s: %s", suc ? "true" : "false", typeid(outcome).name());

		const auto& err{ outcome.GetError() };
		const char* mesg{ err.GetMessage().c_str() };
		const auto code{ err.GetResponseCode() };
		const auto type{ err.GetErrorType() };
		const char* name{ err.GetExceptionName().c_str() };

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
	AWSS3_API WinCseLib::ICSDevice* NewCSDevice(
		const wchar_t* argTempDir, const wchar_t* argIniSection,
		WinCseLib::NamedWorker argWorkers[]);
}

#define StatsIncr(name)				::InterlockedIncrement(& (this->mStats->name))

#define AWS_DEFAULT_REGION			Aws::Region::US_EAST_1

// EOF