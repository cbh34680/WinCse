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
	// �{���� std::atomic<int> �����A�����̎Q�ƒl�Ȃ̂Ō����łȂ��Ă� OK
	// operator=() �̎������ȗ� :-)
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
// open() ���Ă΂ꂽ�Ƃ��� UParam �Ƃ��� PTFS_FILE_CONTEXT �ɕۑ�����������
// close() �ō폜�����
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

	// �����Q�Ɨp�t�@�C���E�n���h��
	WCSE::FileHandle mRefFile;
	WCSE::FileHandle mRefDir;

	// �t�@�C���������̔r������
	struct PrepareLocalFileShare : public SharedBase { };
	ShareStore<PrepareLocalFileShare> mPrepareLocalFileShare;

	// �V���b�g�_�E���v�۔���̂��߃|�C���^
	std::unique_ptr<Aws::SDKOptions> mSDKOptions;

	// S3 �N���C�A���g
	ClientPtr mClient;

	// Worker �擾
	const std::unordered_map<std::wstring, WCSE::IWorker*> mWorkers;

	WCSE::IWorker* getWorker(const std::wstring& argName)
	{
		return mWorkers.at(argName);
	}

	void addTasks(CALLER_ARG0);
	void notifListener();

	// �o�P�b�g���t�B���^
	std::vector<std::wregex> mBucketFilters;
	bool isInBucketFilters(const std::wstring& arg);

	// �o�P�b�g����֘A
	void clearBucketCache(CALLER_ARG0);
	bool reloadBucketCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	void reportBucketCache(CALLER_ARG FILE* fp);
	std::wstring getBucketLocation(CALLER_ARG const std::wstring& bucketName);

	// �I�u�W�F�N�g����֘A
	void reportObjectCache(CALLER_ARG FILE* fp);
	int deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold);
	int clearObjectCache(CALLER_ARG0);
	int deleteObjectCache(CALLER_ARG const WCSE::ObjectKey& argObjKey);

	// AWS SDK API �����s
	WCSE::DirInfoType apicallHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);
	bool apicallListObjectsV2(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		bool argDelimiter, int argLimit, WCSE::DirInfoListType* pDirInfoList);

	//
	bool unsafeHeadBucket(CALLER_ARG const std::wstring& bucketName,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);
	bool unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */,
		const std::vector<std::wstring>& options);

	//
	bool unsafeHeadObjectWithCache(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */);
	bool unsafeListObjectsWithCache(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const Purpose purpose, WCSE::DirInfoListType* pDirInfoList /* nullable */);
	bool unsafeGetPositiveCache_File(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoType* pDirInfo);
	bool unsafeIsInNegativeCache_File(CALLER_ARG const WCSE::ObjectKey& argObjKey);

	// Read �֘A

	NTSTATUS prepareLocalFile_simple(CALLER_ARG OpenContext* ctx, UINT64 argOffset, ULONG argLength);
	bool doMultipartDownload(CALLER_ARG OpenContext* ctx, const std::wstring& localPath);

public:
	// �O������Ăяo����邽�� override �ł͂Ȃ��� public �̃��\�b�h

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

	bool PreCreateFilesystem(FSP_SERVICE *Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;
	bool OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem, PCWSTR PtfsPath) override;
	void OnSvcStop() override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;
	bool listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */) override;

	bool headObject_File(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;
	bool headObject_Dir(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;

	bool listObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList /* nullable */) override;

	bool deleteObject(CALLER_ARG const WCSE::ObjectKey& argObjKey) override;

	bool putObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		PCWSTR sourceFile /* nullable */) override;

	WCSE::CSDeviceContext* create(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& fileInfo, UINT32 CreateOptions,
		UINT32 GrantedAccess, UINT32 FileAttributes) override;

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
	// �t�@�C��/�f�B���N�g���ɓ���
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