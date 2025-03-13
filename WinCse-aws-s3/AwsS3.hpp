#pragma once

#include "WinCseLib.h"
#include "aws_sdk_s3.h"

//
// "Windows.h" �Œ�`����Ă��� GetObject �� aws-sdk-cpp �̃��\�b�h����
// �o�b�e�B���O���ăR���p�C���ł��Ȃ��̂��Ƃ����
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
	// �{���� std::atomic<int> �����A�����̎Q�ƒl�Ȃ̂Ō����łȂ��Ă� OK
	// operator=() �̎������ȗ� :-)
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

	bool mReadonlyFilesystem = false;
	UINT32 mDefaultFileAttributes = 0;

	// �V���b�g�_�E���v�۔���̂��߃|�C���^�ɂ��Ă���
	std::unique_ptr<Aws::SDKOptions> mSDKOptions;

	// S3 �N���C�A���g
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

	NTSTATUS readObject_Simple(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
	NTSTATUS readObject_Multipart(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
	bool doMultipartDownload(CALLER_ARG WinCseLib::IOpenContext* argOpenContext, const std::wstring& localPath);

	void notifListener();

	// cleanup() �ŗ��p
	int deleteCacheByObjKey(CALLER_ARG const WinCseLib::ObjectKey& argObjKey);

public:
	// Worker ����Ăяo����邽�� override �ł͂Ȃ��� public �̃��\�b�h

	void OnIdleTime(CALLER_ARG0);

	int64_t prepareLocalCacheFile(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const FileOutputMeta& argMeta);

protected:
	bool isInBucketFiltersW(const std::wstring& arg);
	bool isInBucketFiltersA(const std::string& arg);

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

	WinCseLib::IOpenContext* open(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& FileInfo, const UINT32 CreateOptions, const UINT32 GrantedAccess) override;

	void close(CALLER_ARG WinCseLib::IOpenContext* argOpenContext) override;

	NTSTATUS readObject(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	NTSTATUS remove(CALLER_ARG WinCseLib::IOpenContext* argOpenContext, BOOLEAN argDeleteFile) override;

	void cleanup(CALLER_ARG WinCseLib::IOpenContext* argOpenContext, ULONG argFlags) override;

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

	// �f�B���N�g���ɓ���
	DirInfoType makeDirInfo_dir(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime);
};

//
// open() ���Ă΂ꂽ�Ƃ��� UParam �Ƃ��� PTFS_FILE_CONTEXT �ɕۑ�����������
// close() �ō폜�����
//
struct OpenContext : public WinCseLib::IOpenContext
{
	WINCSE_DEVICE_STATS* mStats;
	const std::wstring mCacheDataDir;
	WinCseLib::ObjectKey mObjKey;
	FSP_FSCTL_FILE_INFO mFileInfo;
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;
	HANDLE mLocalFile = INVALID_HANDLE_VALUE;

	OpenContext(
		WINCSE_DEVICE_STATS* argStats,
		const std::wstring& argCacheDataDir,
		const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const UINT32 argCreateOptions,
		const UINT32 argGrantedAccess)
		:
		mStats(argStats),
		mCacheDataDir(argCacheDataDir),
		mFileInfo(argFileInfo),
		mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess)
	{
		if (FA_IS_DIR(mFileInfo.FileAttributes))
		{
			mObjKey = argObjKey.toDir();
		}
		else
		{
			mObjKey = argObjKey;
		}
	}

	bool isDir() const { return FA_IS_DIR(mFileInfo.FileAttributes); }
	bool isFile() const { return !isDir(); }
	std::wstring getRemotePath() const { return mObjKey.str(); }

	std::wstring getLocalPath() const;

	bool openLocalFile(const DWORD argDesiredAccess, const DWORD argCreationDisposition);
	bool setLocalFileTime(UINT64 argCreationTime);
	void closeLocalFile();

	~OpenContext()
	{
		closeLocalFile();
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