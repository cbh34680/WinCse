#pragma once

#include "GcpGsInternal.h"

namespace CSEGGS
{

class GcpGsClient : public CSEDVC::IApiClient
{
protected:
	CSELIB::IWorker* const									mDelayedWorker;
	const CSEDVC::RuntimeEnv* const							mRuntimeEnv;
	const std::wstring										mProjectId;
	const std::unique_ptr<google::cloud::storage::Client>	mGsClient;

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
		// GCP は異なるリージョンのバケットを参照できる

		return true;
	}

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

// EOF