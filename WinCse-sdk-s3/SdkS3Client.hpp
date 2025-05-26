#pragma once

#include "CSDevice.hpp"
#include "aws_sdk_s3_client.h"

#ifdef WINCSESDKS3_EXPORTS
#define WINCSESDKS3_API __declspec(dllexport)
#else
#define WINCSESDKS3_API __declspec(dllimport)
#endif

namespace CSESS3
{

using UploadFilePartType = CSELIB::FilePart<std::optional<Aws::String>>;

class SdkS3Client : public CSEDVC::IApiClient
{
protected:
	CSELIB::IWorker* const				mDelayedWorker;
	const CSEDVC::RuntimeEnv* const		mRuntimeEnv;
	std::wstring						mClientRegion;
	Aws::S3::S3Client* const			mS3Client;

	virtual std::string getDefaultBucketRegion() const
	{
		// �Â��o�P�b�g�i2008�N�ȑO�j
		// ������S3�ł́A�o�P�b�g�͂��ׂāuUS Standard�i���݂� us-east-1�j�v�ɍ쐬����Ă��܂����B
		// ���̍��͖����I�Ƀ��[�W�����w������邱�Ƃ��Ȃ��������߁A�Â��o�P�b�g�𑀍삷��ۂɃ��[�W������񂪌����ɂ������Ƃ�����܂��B

		return Aws::Region::US_EAST_1;
	}

	WINCSESDKS3_API bool uploadSimple(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath);
	WINCSESDKS3_API bool PutObjectInternal(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath);

public:
	SdkS3Client(const CSEDVC::RuntimeEnv* argRuntimeEnv, CSELIB::IWorker* argDelayedWorker, const std::wstring& argClientRegion, Aws::S3::S3Client* argS3Client)
		:
		mRuntimeEnv(argRuntimeEnv),
		mDelayedWorker(argDelayedWorker),
		mClientRegion(argClientRegion),
		mS3Client(argS3Client)
	{
	}

	bool canAccessRegion(CALLER_ARG const std::wstring& argBucketRegion) override
	{
		NEW_LOG_BLOCK();

		if (mRuntimeEnv->IgnoreBucketRegion)
		{
			traceW(L"ignore bucket region");
			return true;
		}

		// �N���C�A���g�̃��[�W�����ƈ�v�����Ƃ��̓A�N�Z�X�\�ȃ��[�W�����Ɣ��f

		const auto ret = argBucketRegion == mClientRegion;

		traceW(L"argBucketRegion=%s mClientRegion=%s ret=%s", argBucketRegion.c_str(), mClientRegion.c_str(), BOOL_CSTRW(ret));

		return ret;
	}

	WINCSESDKS3_API std::optional<Aws::String> uploadPart(CALLER_ARG const CSELIB::ObjectKey& argObjKey,
		const std::filesystem::path& argInputPath, const Aws::String& argUploadId, const std::shared_ptr<UploadFilePartType>& argFilePart);

	// AWS SDK API �����s

	WINCSESDKS3_API bool ListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSESDKS3_API bool GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pBuketRegion) override;
	WINCSESDKS3_API bool HeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) override;
	WINCSESDKS3_API bool ListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSESDKS3_API bool DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) override;
	WINCSESDKS3_API bool DeleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) override;
	WINCSESDKS3_API bool PutObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath) override;
	WINCSESDKS3_API bool CopyObject(CALLER_ARG const CSELIB::ObjectKey& argSrcObjKey, const CSELIB::ObjectKey& argDstObjKey) override;
	WINCSESDKS3_API CSELIB::FILEIO_LENGTH_T GetObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_LENGTH_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) override;
};

}	// namespace CSESS3

template<typename T>
bool IsSuccess(const T& outcome)
{
	NEW_LOG_BLOCK();

	const bool isSuccess = outcome.IsSuccess();
	traceA("isSuccess=%s name=%s", BOOL_CSTRA(isSuccess), typeid(outcome).name());

	if (!isSuccess)
	{
		const auto& err{ outcome.GetError() };

		const auto mesg{ err.GetMessage().c_str() };
		const auto code{ err.GetResponseCode() };
		const auto type{ err.GetErrorType() };
		const auto name{ err.GetExceptionName().c_str() };

		switch (static_cast<int>(code))
		{
			case 404:
			{
				traceA("warn: type=%d, code=%d, name=%s, message=%s", type, code, name, mesg);
				break;
			}
			default:
			{
				errorA("error: type=%d, code=%d, name=%s, message=%s", type, code, name, mesg);
				break;
			}
		}
	}

	return isSuccess;
}

template<typename ReturnT, typename RequestT>
ReturnT executeWithRetry(
	Aws::S3::S3Client* argClient,
	ReturnT (Aws::S3::S3Client::*argMethod)(const RequestT&) const,
	const RequestT& argRequest,
	int argMaxRetryCount)
{
	NEW_LOG_BLOCK();

	ReturnT outcome;

	bool shouldRetry = true;
	DWORD waitSec = 1;
	int i = 0;

	do
	{
		outcome = (argClient->*argMethod)(argRequest);
		if (outcome.IsSuccess())
		{
			traceW(L"success");
			shouldRetry = false;
		}
		else
		{
			const auto& err{ outcome.GetError() };

			const auto mesg{ err.GetMessage().c_str() };
			const auto code{ err.GetResponseCode() };
			const auto type{ err.GetErrorType() };
			const auto name{ err.GetExceptionName().c_str() };
			shouldRetry = err.ShouldRetry();

			switch (static_cast<int>(code))
			{
				case 404:
				{
					traceA("fault: type=%d code=%d name=%s message=%s retry=%s", type, code, name, mesg, BOOL_CSTRA(shouldRetry));
					break;
				}
				default:
				{
					errorA("fault: type=%d code=%d name=%s message=%s retry=%s", type, code, name, mesg, BOOL_CSTRA(shouldRetry));
					break;
				}
			}
		}

		if (!shouldRetry)
		{
			break;
		}

		traceW(L"retry: %d/%d", i + 1, argMaxRetryCount);
		::Sleep(waitSec * 1000);

		waitSec *= 2;
		i++;
	}
	while (i < argMaxRetryCount);

	return outcome;
}

// EOF