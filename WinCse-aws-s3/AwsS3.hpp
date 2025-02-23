#pragma once

#include "aws_sdk_s3.hpp"

#ifdef WINCSEAWSS3_EXPORTS
#define AWSS3_API __declspec(dllexport)
#else
#define AWSS3_API __declspec(dllimport)
#endif

#include "BucketCache.hpp"
#include "ObjectCache.hpp"

#include <string>
#include <regex>
#include <vector>
#include <memory>

// �ő�o�P�b�g�\����
#define DEFAULT_MAX_BUCKETS			(15)

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

extern const char* AWS_DEFAULT_REGION;			// Aws::Region::US_EAST_1

class ClientPtr : public std::shared_ptr<Aws::S3::S3Client>
{
	// �{���� std::atomic<int> �����A�����̎Q�ƒl�Ȃ̂Ō����łȂ��Ă� OK
	// operator=() �̎������ȗ� :-)
	int mRefCount = 0;

public:
	ClientPtr() = default;

	ClientPtr(Aws::S3::S3Client* client)
		: std::shared_ptr<Aws::S3::S3Client>(client) { }

	Aws::S3::S3Client* operator->() noexcept;

	int getRefCount() const { return mRefCount; }
};

class AwsS3 : public WinCseLib::ICloudStorage
{
private:
	WinCseLib::IWorker* mDelayedWorker;
	WinCseLib::IWorker* mIdleWorker;

	const std::wstring mTempDir;
	const std::wstring mIniSection;
	std::wstring mWorkDir;
	std::wstring mCacheDir;
	UINT64 mWorkDirTime;
	int mMaxBuckets;
	int mMaxObjects;
	std::wstring mRegion;

	// �V���b�g�_�E���v�۔���̂��߃|�C���^�ɂ��Ă���
	std::shared_ptr<Aws::SDKOptions> mSDKOptions;

	// S3 �N���C�A���g
	struct
	{
		ClientPtr ptr;
	}
	mClient;

	std::vector<std::wregex> mBucketFilters;

	BucketCache mBucketCache;
	ObjectCache mObjectCache;

	std::wstring unsafeGetBucketRegion(CALLER_ARG const std::wstring& bucketName);

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

	bool unsafeListObjectsV2(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
		const int limit, const bool delimiter);

	std::shared_ptr<FSP_FSCTL_DIR_INFO> unsafeHeadObject(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey);

	bool prepareLocalCacheFile(CALLER_ARG
		const std::wstring& bucket, const std::wstring& key, const std::wstring& localPath);

protected:
	bool isInBucketFiltersW(const std::wstring& arg);
	bool isInBucketFiltersA(const std::string& arg);

public:
	AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection,
		WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker);

	~AwsS3();

	bool OnSvcStart(const wchar_t* argWorkDir) override;
	void OnSvcStop() override;
	bool OnPostSvcStart() override;
	void OnIdleTime(CALLER_ARG0);

	bool listBuckets(CALLER_ARG
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
		const std::vector<std::wstring>& options) override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket) override;

	bool listObjects(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
		const int limit, const bool delimiter) override;

	bool headObject(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
		FSP_FSCTL_FILE_INFO* pFileInfo) override;

	bool openFile(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		UINT32 CreateOptions, UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& fileInfo, 
		PVOID* pCSData);

	void closeFile(CALLER_ARG PVOID CSData);

	bool readFile(CALLER_ARG PVOID CSData,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
};

// �t�@�C�������� FSP_FSCTL_DIR_INFO �̃q�[�v�̈�𐶐����A�������̃����o��ݒ肵�ĕԋp
std::shared_ptr<FSP_FSCTL_DIR_INFO> mallocDirInfoW(const std::wstring& key, const std::wstring& bucket);
std::shared_ptr<FSP_FSCTL_DIR_INFO> mallocDirInfoA(const std::string& key, const std::string& bucket);

extern "C"
{
AWSS3_API WinCseLib::ICloudStorage* NewCloudStorage(
	const wchar_t* argTempDir, const wchar_t* argIniSection,
	WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker);
}

// EOF