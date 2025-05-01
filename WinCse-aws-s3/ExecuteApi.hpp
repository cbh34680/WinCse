#pragma once

#include "CSDeviceCommon.h"
#include "RuntimeEnv.hpp"
#include "aws_sdk_s3_client.h"

namespace CSEDAS3
{

class ExecuteApi final
{
private:
	const RuntimeEnv* const				mRuntimeEnv;
	std::unique_ptr<Aws::SDKOptions>	mSdkOptions;
	std::unique_ptr<Aws::S3::S3Client>	mS3Client;

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

	bool isInBucketFilters(const std::wstring& arg) const noexcept;

	CSELIB::DirInfoPtr makeDirInfoOfDir_2(const std::wstring& argFileName, CSELIB::FILETIME_100NS_T argFileTime100ns) const noexcept
	{
		return makeDirInfoOfDir(argFileName, argFileTime100ns, mRuntimeEnv->DefaultFileAttributes);
	}

public:
	// AWS SDK API Çé¿çs

	bool shouldIgnoreFileName(const std::wstring& arg) const noexcept;

	bool Ping(CALLER_ARG0) const;
	bool ListBuckets(CALLER_ARG CSELIB::DirInfoPtrList* pDirInfoList) const noexcept;
	bool GetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion) const noexcept;
	bool HeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtr* pDirInfo) const noexcept;
	bool ListObjectsV2(CALLER_ARG const CSELIB::ObjectKey& argObjKey, bool argDelimiter, int argLimit, CSELIB::DirInfoPtrList* pDirInfoList) const noexcept;
	bool DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) const noexcept;
	bool DeleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) const noexcept;
	bool PutObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) const noexcept;
	CSELIB::FILEIO_LENGTH_T GetObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_LENGTH_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) const noexcept;

public:
	ExecuteApi(const RuntimeEnv* argRuntimeEnv,
		const std::wstring& argRegion, const std::wstring& argAccessKeyId,
		const std::wstring& argSecretAccessKey) noexcept;

	~ExecuteApi();
};

}	// namespace CSEDAS3

#define AWS_DEFAULT_REGION		(Aws::Region::US_EAST_1)

// EOF