#pragma once

#include "WinCseLib.h"
#include "aws_sdk_s3.h"

//
// "Windows.h" で定義されている GetObject と aws-sdk-cpp のメソッド名が
// バッティングしてコンパイルできないのことを回避
//
#ifdef GetObject
#undef GetObject
#endif

#ifdef GetMessage
#undef GetMessage
#endif

#include <regex>
#include "Purpose.h"

struct FileOutputMeta
{
	std::wstring mPath;
	DWORD mCreationDisposition;
	bool mSpecifyRange;
	UINT64 mOffset;
	ULONG mLength;
	bool mSetFileTime;

	FileOutputMeta(
		std::wstring argPath, DWORD argCreationDisposition,
		bool argSpecifyRange, UINT64 argOffset,
		ULONG argLength, bool argSetFileTime)
		:
		mPath(argPath), mCreationDisposition(argCreationDisposition),
		mSpecifyRange(argSpecifyRange), mOffset(argOffset),
		mLength(argLength), mSetFileTime(argSetFileTime)
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
	int mRefCount = 0;

public:
	ClientPtr() = default;

	ClientPtr(Aws::S3::S3Client* client)
		: std::unique_ptr<Aws::S3::S3Client>(client) { }

	Aws::S3::S3Client* operator->() noexcept;

	int getRefCount() const { return mRefCount; }
};

class AwsS3 : public WinCseLib::ICSDevice
{
private:
	WINCSE_DEVICE_STATS* mStats = nullptr;
	WINCSE_DEVICE_STATS mStats_{};

	WinCseLib::IWorker* mDelayedWorker;
	WinCseLib::IWorker* mIdleWorker;

	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::wstring mWorkDir;
	std::wstring mCacheDataDir;
	std::wstring mCacheReportDir;

	UINT64 mWorkDirTime = 0;
	int mMaxBuckets = -1;
	int mMaxObjects = -1;
	std::wstring mRegion;
	FSP_FILE_SYSTEM* mFileSystem = nullptr;

	UINT32 mDefaultFileAttributes = 0;

	// 属性参照用ファイル・ハンドル
	WinCseLib::FileHandleRAII mRefFile;
	WinCseLib::FileHandleRAII mRefDir;

	// シャットダウン要否判定のためポインタにしている
	std::unique_ptr<Aws::SDKOptions> mSDKOptions;

	// S3 クライアント
	struct
	{
		ClientPtr ptr;
	}
	mClient;

	std::vector<std::wregex> mBucketFilters;

	void reloadBukcetsIfNeed(CALLER_ARG0);
	void reportBucketCache(CALLER_ARG FILE* fp);

	void deleteOldObjects(CALLER_ARG
		std::chrono::system_clock::time_point threshold);

	void reportObjectCache(CALLER_ARG FILE* fp);

	std::wstring unlockGetBucketRegion(CALLER_ARG const std::wstring& bucketName);

	DirInfoType apicallHeadObject(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

	bool apicallListObjectsV2(CALLER_ARG const Purpose purpose,
		const WinCseLib::ObjectKey& argObjKey, DirInfoListType* pDirInfoList);

	int unlockDeleteCacheByObjKey(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

	bool unlockHeadObject(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);

	bool unlockHeadObject_File(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);

	bool unlockListObjects(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const Purpose purpose, DirInfoListType* pDirInfoList /* nullable */);

	DirInfoType unlockListObjects_Dir(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

	DirInfoType unlockFindInParentOfDisplay(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

	bool unlockListObjects_Display(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		DirInfoListType* pDirInfoList /* nullable */);

	bool shouldDownload(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& fileInfo, const std::wstring& localPath, bool* pNeedDownload);

	bool readObject_Simple(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
	bool readObject_Multipart(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
	bool doMultipartDownload(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext, const std::wstring& localPath);

	void notifListener();

	// cleanup() で利用
	int deleteCacheByObjKey(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

public:
	// Worker から呼び出されるため override ではないが public のメソッド

	void OnIdleTime(CALLER_ARG0);

	int64_t prepareLocalCacheFile(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const FileOutputMeta& argMeta);

protected:
	bool isInBucketFilters(const std::wstring& arg);

public:
	AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection,
		WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker);

	~AwsS3();

	void queryStats(WINCSE_DEVICE_STATS* pStats) override
	{
		*pStats = *mStats;
	}

	bool PreCreateFilesystem(const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;
	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket) override;

	bool listBuckets(CALLER_ARG DirInfoListType* pDirInfoList /* nullable */,
		const std::vector<std::wstring>& options) override;

	bool headObject(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;

	bool listObjects(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		DirInfoListType* pDirInfoList /* nullable */) override;

	WinCseLib::CSDeviceContext* create(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 FileAttributes,
		FSP_FSCTL_FILE_INFO* pFileInfo) override;

	WinCseLib::CSDeviceContext* open(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo) override;

	void close(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext) override;

	bool readObject(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	bool writeObject(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length,
		BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) override;

	bool remove(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext, BOOLEAN argDeleteFile) override;

	void cleanup(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext, ULONG argFlags) override;

private:
	template<typename T>
	bool outcomeIsSuccess(const T& outcome)
	{
		NEW_LOG_BLOCK();

		const bool suc = outcome.IsSuccess();

		traceA("outcome.IsSuccess()=%s: %s", suc ? "true" : "false", typeid(outcome).name());

		if (!suc)
		{
			const auto& err{ outcome.GetError() };
			const char* mesg{ err.GetMessage().c_str() };
			const auto code{ err.GetResponseCode() };
			const auto type{ err.GetErrorType() };
			const char* name{ err.GetExceptionName().c_str() };

			traceA("error: type=%d, code=%d, name=%s, message=%s", type, code, name, mesg);
		}

		return suc;
	}

	// ディレクトリに特化
	DirInfoType makeDirInfo_dir(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime);
};

//
// open() が呼ばれたときに UParam として PTFS_FILE_CONTEXT に保存する内部情報
// close() で削除される
//
struct OpenContext : public WinCseLib::CSDeviceContext
{
	WINCSE_DEVICE_STATS* mStats;
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;

	OpenContext(
		WINCSE_DEVICE_STATS* argStats,
		const std::wstring& argCacheDataDir,
		const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const UINT32 argCreateOptions,
		const UINT32 argGrantedAccess)
		:
		CSDeviceContext(argCacheDataDir, argObjKey, argFileInfo),
		mStats(argStats),
		mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess)
	{
	}

	bool openLocalFile(const DWORD argDesiredAccess, const DWORD argCreationDisposition);
};

struct CreateContext : public WinCseLib::CSDeviceContext
{
	WINCSE_DEVICE_STATS* mStats;

	CreateContext(
		WINCSE_DEVICE_STATS* argStats,
		const std::wstring& argCacheDataDir,
		const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo)
		:
		CSDeviceContext(argCacheDataDir, argObjKey, argFileInfo),
		mStats(argStats)
	{
	}
};

#ifdef WINCSEAWSS3_EXPORTS
#define AWSS3_API __declspec(dllexport)
#else
#define AWSS3_API __declspec(dllimport)
#endif

extern "C"
{
	AWSS3_API WinCseLib::ICSDevice* NewCSDevice(
		const wchar_t* argTempDir, const wchar_t* argIniSection,
		WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker);
}

#define StatsIncr(name)					::InterlockedIncrement(& (this->mStats->name))

#define StatsIncrBool(b, sname, ename) \
	if (b) ::InterlockedIncrement(& (this->mStats->sname)); \
	else   ::InterlockedIncrement(& (this->mStats->ename))

#define AWS_DEFAULT_REGION			Aws::Region::US_EAST_1

// EOF