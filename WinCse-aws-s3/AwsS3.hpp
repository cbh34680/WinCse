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

	std::wstring unsafeGetBucketRegion(CALLER_ARG const std::wstring& bucketName);

	DirInfoType apicallHeadObject(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey);

	bool apicallListObjectsV2(CALLER_ARG const Purpose purpose,
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoListType* pDirInfoList);

	bool unsafeHeadObject(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		bool alsoSearchCache, FSP_FSCTL_FILE_INFO* pFileInfo);

	bool unsafeHeadObject_File(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		FSP_FSCTL_FILE_INFO* pFileInfo);

	bool unsafeListObjects(CALLER_ARG const Purpose purpose,
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoListType* pDirInfoList);

	DirInfoType unsafeListObjects_Dir(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey);

	DirInfoType findFileInParentDirectry(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey);

	bool unsafeListObjects_Display(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoListType* pDirInfoList);

	bool shouldDownload(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		const std::wstring& localPath, FSP_FSCTL_FILE_INFO* pFileInfo,
		bool* pNeedGet);

	bool readFile_Simple(CALLER_ARG PVOID UParam,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
	bool readFile_Multipart(CALLER_ARG PVOID UParam,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);

	void notifListener();

	// shouldDownload() で使用
	bool headObject_File_SkipCacheSearch(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);

public:
	// Worker から呼び出されるため override ではないが public のメソッド

	void OnIdleTime(CALLER_ARG0);

	int64_t prepareLocalCacheFile(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey, const FileOutputMeta& argMeta);


protected:
	bool isInBucketFiltersW(const std::wstring& arg);
	bool isInBucketFiltersA(const std::string& arg);

public:
	AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection,
		WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker);

	~AwsS3();

	// ReadFileContext から直接参照するので public
	WINCSE_DEVICE_STATS mStats = { };

	void queryStats(WINCSE_DEVICE_STATS* pStats) override
	{
		*pStats = mStats;
	}

	bool PreCreateFilesystem(const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;
	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket) override;

	bool listBuckets(CALLER_ARG
		DirInfoListType* pDirInfoList,
		const std::vector<std::wstring>& options) override;

	bool headObject(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
		FSP_FSCTL_FILE_INFO* pFileInfo) override;

	bool listObjects(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoListType* pDirInfoList) override;

	bool openFile(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& fileInfo, 
		PVOID* pUParam) override;

	void closeFile(CALLER_ARG PVOID UParam) override;

	bool readFile(CALLER_ARG PVOID UParam,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

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
	DirInfoType mallocDirInfoW_dir(
		const std::wstring& argKey, const std::wstring& argBucket, const UINT64 argFileTime);
};

// ファイル名から FSP_FSCTL_DIR_INFO のヒープ領域を生成し、いくつかのメンバを設定して返却
DirInfoType mallocDirInfoW(const std::wstring& argKey, const std::wstring& argBucket);
DirInfoType mallocDirInfoA(const std::string& argKey, const std::string& argBucket);


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

#define StatsIncr(name)					::InterlockedIncrement(& (this->mStats.name))

#define StatsIncrBool(b, sname, ename) \
	if (b) ::InterlockedIncrement(& (this->mStats.sname)); \
	else   ::InterlockedIncrement(& (this->mStats.ename))

#define AWS_DEFAULT_REGION			Aws::Region::US_EAST_1


//
// openFIle() が呼ばれたときに UParam として PTFS_FILE_CONTEXT に保存する内部情報
// closeFile() で削除される
//

struct OpenFileContext
{
	WINCSE_DEVICE_STATS& mStats;
	const std::wstring mBucket;
	const std::wstring mKey;
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;
	FSP_FSCTL_FILE_INFO mFileInfo;

	std::wstring mGuardString;

	HANDLE mFile = INVALID_HANDLE_VALUE;
	UINT64 mLastOffset = 0ULL;

	OpenFileContext(
		WINCSE_DEVICE_STATS& argStats,
		const std::wstring& argBucket, const std::wstring& argKey,
		const UINT32 argCreateOptions, const UINT32 argGrantedAccess,
		const FSP_FSCTL_FILE_INFO& argFileInfo)
		:
		mStats(argStats),
		mBucket(argBucket), mKey(argKey), mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess), mFileInfo(argFileInfo)
	{
		mGuardString = mBucket + L'/' + mKey;
	}

	std::wstring getGuardString() const
	{
		return mGuardString;
	}

	~OpenFileContext();
};

// EOF