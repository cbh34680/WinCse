#pragma once

#include "GcpGsCommon.h"

namespace CSEGGS
{

//using UploadFilePartType = CSELIB::FilePart<google::cloud::StatusOr<google::cloud::storage::ResumableUploadResult>>;

class GcpGsClient : public CSEDVC::IApiClient
{
protected:
	CSELIB::IWorker* const									mDelayedWorker;
	const CSEDVC::RuntimeEnv* const							mRuntimeEnv;
	const std::wstring										mProjectId;
	const std::unique_ptr<google::cloud::storage::Client>	mGsClient;

	WINCSEGCPGS_API bool uploadSimple(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath);
	WINCSEGCPGS_API bool PutObjectInternal(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath);


public:
	GcpGsClient(const CSEDVC::RuntimeEnv* argRuntimeEnv, CSELIB::IWorker* argDelayedWorker, const std::wstring& argProjectId)
		:
		mDelayedWorker(argDelayedWorker),
		mRuntimeEnv(argRuntimeEnv),
		mProjectId(argProjectId),
		mGsClient(std::make_unique<google::cloud::storage::Client>())
	{
	}

	bool canAccessRegion(CALLER_ARG const std::wstring&) override
	{
		// GCP では同じクライアントで全てのリージョンのバケットにアクセスできる

		return true;
	}

	/*
	WINCSEGCPGS_API std::optional<Aws::String> uploadPart(CALLER_ARG const CSELIB::ObjectKey& argObjKey,
		const std::filesystem::path& argInputPath, const Aws::String& argUploadId, const std::shared_ptr<UploadFilePartType>& argFilePart);
	*/

	// SDK API 呼び出しの実装

	WINCSEGCPGS_API bool ListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEGCPGS_API bool GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pBuketRegion) override;
	WINCSEGCPGS_API bool HeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) override;
	WINCSEGCPGS_API bool ListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEGCPGS_API bool DeleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) override;
	WINCSEGCPGS_API bool PutObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath) override;
	WINCSEGCPGS_API bool CopyObject(CALLER_ARG const CSELIB::ObjectKey& argSrcObjKey, const CSELIB::ObjectKey& argDstObjKey) override;
	WINCSEGCPGS_API CSELIB::FILEIO_LENGTH_T GetObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_LENGTH_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) override;
};

}	// namespace CSEGGS

bool IsSuccess(const google::cloud::Status& status);

template <typename T>
bool IsSuccess(const T& result)
{
	NEW_LOG_BLOCK();

	const bool isSuccess = result.ok();
	traceA("isSuccess=%s name=%s", BOOL_CSTRA(isSuccess), typeid(result).name());

	if (!isSuccess)
	{
		IsSuccess(result.status());
	}

	return isSuccess;
}

// EOF