#pragma once

#include <vector>
#include <string>
#include <memory>


struct ICloudStorage : public IService
{
	virtual ~ICloudStorage() = 0;

	virtual void updateVolumeParams(FSP_FSCTL_VOLUME_PARAMS* VolumeParams) = 0;

	virtual bool listBuckets(CALLER_ARG
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
		const std::vector<std::wstring>& options) = 0;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket) = 0;

	virtual bool listObjects(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
		const int limit, const bool delimiter) = 0;

	virtual bool headObject(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		FSP_FSCTL_FILE_INFO* pFileInfo) = 0;

	virtual HANDLE openObject(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		UINT32 CreateOptions, UINT32 GrantedAccess) = 0;
};

// EOF