#pragma once

#include "WinCseLib.h"
#include "RuntimeEnv.hpp"
#include "FileOutputParams.hpp"
#include "aws_sdk_s3_client.h"

class ExecuteApi
{
private:
	const RuntimeEnv* const mRuntimeEnv;

	// S3 クライアント
	std::unique_ptr<Aws::SDKOptions> mSdkOptions;
	std::unique_ptr<Aws::S3::S3Client> mS3Client;

	template<typename T>
	bool outcomeIsSuccess(const T& outcome) const noexcept
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

	bool isInBucketFilters(const std::wstring& arg) const noexcept
	{
		if (mRuntimeEnv->BucketFilters.empty())
		{
			return true;
		}

		const auto it = std::find_if(mRuntimeEnv->BucketFilters.cbegin(), mRuntimeEnv->BucketFilters.cend(), [&arg](const auto& re)
		{
			return std::regex_match(arg, re);
		});

		return it != mRuntimeEnv->BucketFilters.cend();
	}

	WCSE::DirInfoType makeDirInfoDir2(const std::wstring& argFileName, UINT64 argFileTime) const noexcept
	{
		return WCSE::makeDirInfo(argFileName, argFileTime, FILE_ATTRIBUTE_DIRECTORY | mRuntimeEnv->DefaultFileAttributes);
	}

public:
	// AWS SDK API を実行

	bool Ping(CALLER_ARG0) const;

	bool ListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList) const noexcept;

	bool GetBucketRegion(CALLER_ARG
		const std::wstring& argBucketName, std::wstring* pBucketRegion) const noexcept;

	bool HeadObject(CALLER_ARG
		const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pDirInfo) const noexcept;

	bool ListObjectsV2(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		bool argDelimiter, int argLimit, WCSE::DirInfoListType* pDirInfoList) const noexcept;

	bool DeleteObjects(CALLER_ARG
		const std::wstring& argBucket, const std::list<std::wstring>& argKeys) const noexcept;

	bool DeleteObject(CALLER_ARG const WCSE::ObjectKey& argObjKey) const noexcept;

	bool PutObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) const noexcept;

	INT64 GetObjectAndWriteToFile(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FileOutputParams& argFOParams) const noexcept;

public:
	ExecuteApi(const RuntimeEnv* argRuntimeEnv,
		const std::wstring& argRegion, const std::wstring& argAccessKeyId,
		const std::wstring& argSecretAccessKey) noexcept;

	~ExecuteApi();
};

#define AWS_DEFAULT_REGION		(Aws::Region::US_EAST_1)

// EOF