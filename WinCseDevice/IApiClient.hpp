#pragma once

namespace CSEDVC {

struct IApiClient
{
	virtual ~IApiClient() = default;
	virtual bool canAccessRegion(CALLER_ARG const std::wstring& argRegion) = 0;

	virtual bool ListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) = 0;
	virtual bool GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pRegion) = 0;
	virtual bool HeadObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) = 0;
	virtual bool ListObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) = 0;
	virtual bool DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) = 0;
	virtual bool DeleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) = 0;
	virtual bool PutObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) = 0;
	virtual CSELIB::FILEIO_LENGTH_T GetObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_LENGTH_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) = 0;
};

}	// namespace CSEDVC

// EOF