#pragma once

#include "WinCseLib.h"
#include "aws_sdk_s3_client.h"
#include "RuntimeEnv.hpp"
#include "FileOutputParams.hpp"
#include <regex>

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

		return it != mRuntimeEnv->BucketFilters.end();
	}

	WCSE::DirInfoType makeDirInfoDir(const std::wstring& argFileName, UINT64 argFileTime)
	{
		return WCSE::makeDirInfo(argFileName, argFileTime, FILE_ATTRIBUTE_DIRECTORY | mRuntimeEnv->DefaultFileAttributes);
	}

public:
	// AWS SDK API を実行

	bool Ping(CALLER_ARG0);

	bool ListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList);

	bool GetBucketRegion(CALLER_ARG
		const std::wstring& argBucketName, std::wstring* pBucketRegion);

	bool HeadObject(CALLER_ARG
		const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pDirInfo);

	bool ListObjectsV2(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		bool argDelimiter, int argLimit, WCSE::DirInfoListType* pDirInfoList);

	bool DeleteObjects(CALLER_ARG
		const std::wstring& argBucket, const std::list<std::wstring>& argKeys);

	bool DeleteObject(CALLER_ARG const WCSE::ObjectKey& argObjKey);

	bool PutObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath);

	INT64 GetObjectAndWriteToFile(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FileOutputParams& argFOParams);

public:
	ExecuteApi(const RuntimeEnv* argRuntimeEnv,
		const std::wstring& argRegion, const std::wstring& argAccessKeyId,
		const std::wstring& argSecretAccessKey) noexcept;

	~ExecuteApi();
};

#define AWS_DEFAULT_REGION		(Aws::Region::US_EAST_1)

// EOF