#pragma once

#include "SdkS3Common.h"
#include "RuntimeEnv.hpp"
#include "aws_sdk_s3_client.h"

namespace CSESS3
{

using UploadFilePartType = CSELIB::FilePart<std::optional<Aws::String>>;

class ExecuteApi final
{
private:
	CSELIB::IWorker*					mDelayedWorker;
	const RuntimeEnv* const				mRuntimeEnv;
	Aws::S3::S3Client*					mS3Client;

	WINCSESDKS3_API bool isInBucketFilters(const std::wstring& argBucket) const;

	WINCSESDKS3_API bool uploadSimple(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath);
	WINCSESDKS3_API bool uploadMultipart(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath);

public:
	// AWS SDK API Çé¿çs

	WINCSESDKS3_API bool shouldIgnoreFileName(const std::filesystem::path& argWinPath) const;

	WINCSESDKS3_API bool Ping(CALLER_ARG0) const;
	WINCSESDKS3_API bool ListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) const;
	WINCSESDKS3_API bool GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pRegion) const;
	WINCSESDKS3_API bool HeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) const;
	WINCSESDKS3_API bool ListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) const;
	WINCSESDKS3_API bool DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) const;
	WINCSESDKS3_API bool DeleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const;
	WINCSESDKS3_API bool PutObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath);
	WINCSESDKS3_API CSELIB::FILEIO_LENGTH_T GetObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_LENGTH_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) const;

public:
	WINCSESDKS3_API ExecuteApi(CSELIB::IWorker* argDelayedWorker, const RuntimeEnv* argRuntimeEnv, Aws::S3::S3Client* argS3Client);

	WINCSESDKS3_API std::optional<Aws::String> uploadPart(CALLER_ARG const CSELIB::ObjectKey& argObjKey,
		const std::filesystem::path& argInputPath, const Aws::String& argUploadId, const std::shared_ptr<UploadFilePartType>& argFilePart);
};

template<typename T>
bool outcomeIsSuccess(const T& outcome)
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

		if (static_cast<int>(code) == 404)
		{
			traceA("error: type=%d, code=%d, name=%s, message=%s", type, code, name, mesg);
		}
		else
		{
			errorA("error: type=%d, code=%d, name=%s, message=%s", type, code, name, mesg);
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

			if (static_cast<int>(code) == 404)
			{
				traceA("fault: type=%d code=%d name=%s message=%s retry=%s", type, code, name, mesg, BOOL_CSTRA(shouldRetry));
			}
			else
			{
				errorA("fault: type=%d code=%d name=%s message=%s retry=%s", type, code, name, mesg, BOOL_CSTRA(shouldRetry));
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

}	// namespace CSESS3

#define AWS_DEFAULT_REGION		(Aws::Region::US_EAST_1)

// EOF