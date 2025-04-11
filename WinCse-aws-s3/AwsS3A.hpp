#pragma once

#include "AwsS3B.hpp"
#include "aws_sdk_s3.h"
#include <regex>

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

	int getRefCount() const noexcept { return mRefCount; }
};

struct FileOutputParams
{
	const std::wstring mPath;
	const DWORD mCreationDisposition;
	const UINT64 mOffset;
	const ULONG mLength;

	FileOutputParams(std::wstring argPath, DWORD argCreationDisposition,
		UINT64 argOffset, ULONG argLength)
		:
		mPath(argPath),
		mCreationDisposition(argCreationDisposition),
		mOffset(argOffset),
		mLength(argLength)
	{
	}

	FileOutputParams(std::wstring argPath, DWORD argCreationDisposition)
		:
		mPath(argPath),
		mCreationDisposition(argCreationDisposition),
		mOffset(0ULL),
		mLength(0UL)
	{
	}

	UINT64 getOffsetEnd() const noexcept
	{
		// GetObject.SetRange �̎w��� "�J�n�I�t�Z�b�g - �I���I�t�Z�b�g" �Ȃ̂�
		// �擪�� 1 byte ���w�肷��ꍇ�� "bytes=0-0" �ƂȂ�
		//
		// mLength �� 0 �̏ꍇ�ɂ���𓖂Ă͂߂�� "mOffset + 0 - 1" �ƂȂ�
		// ����͕s���R�ł��邪 UINT64 �̂��߁A�G���[�l�Ƃ��ĕ��l���߂��Ȃ�
		//
		// ���̂��߁AmLength �̒l�ɂ�蓮���ς��Ă���

		if (mLength > 0)
		{
			return mOffset + mLength - 1;
		}
		else
		{
			return mOffset;
		}
	}

	std::wstring str() const;
};

class AwsS3A : public AwsS3B
{
private:
	// �ڑ����[�W����
	std::wstring mRegion;

	// �V���b�g�_�E���v�۔���̂��߃|�C���^
	std::unique_ptr<Aws::SDKOptions> mSDKOptions;

	// S3 �N���C�A���g
	ClientPtr mClient;

	// �o�P�b�g���t�B���^
	std::vector<std::wregex> mBucketFilters;

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

protected:
	std::wstring getClientRegion() const noexcept
	{
		return mRegion;
	}

	int getClientRefCount() const noexcept
	{
		return mClient.getRefCount();
	}

	bool isInBucketFilters(const std::wstring& arg) const noexcept
	{
		if (mBucketFilters.empty())
		{
			return true;
		}

		const auto it = std::find_if(mBucketFilters.cbegin(), mBucketFilters.cend(), [&arg](const auto& re)
		{
			return std::regex_match(arg, re);
		});

		return it != mBucketFilters.end();
	}

	// AWS SDK API �����s

	bool apicallListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList);

	bool apicallGetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion);

	bool apicallHeadObject(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pDirInfo);

	bool apicallListObjectsV2(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		bool argDelimiter, int argLimit, WCSE::DirInfoListType* pDirInfoList);

	bool apicallDeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys);
	bool apicallDeleteObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);

	bool apicallPutObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo, const std::wstring& argFilePath);

public:
	INT64 apicallGetObjectAndWriteToFile(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FileOutputParams& argFOParams);

public:
	using AwsS3B::AwsS3B;

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;


};

#define AWS_DEFAULT_REGION			Aws::Region::US_EAST_1

// EOF