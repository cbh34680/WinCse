#pragma once

#include <vector>
#include <string>
#include <memory>

namespace WinCseLib {

struct WINCSELIB_API ICloudStorage : public IService
{
	virtual ~ICloudStorage() = default;

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

	virtual bool openFile(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		UINT32 CreateOptions, UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& fileInfo, 
		PVOID* pCSData) = 0;

	virtual void closeFile(CALLER_ARG PVOID CSData) = 0;

	virtual bool readFile(CALLER_ARG PVOID CSData,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;
};

} // namespace WinCseLib

// EOF