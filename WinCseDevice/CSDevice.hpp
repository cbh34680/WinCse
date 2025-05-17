#pragma once

#include "CSDeviceBase.hpp"

namespace CSEDVC
{

class CSDevice : public CSDeviceBase
{
private:
	WINCSEDEVICE_API bool headObjectFromCache_(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);

public:
	using CSDeviceBase::CSDeviceBase;

	WINCSEDEVICE_API ~CSDevice();

	WINCSEDEVICE_API bool headBucket(CALLER_ARG const std::wstring& argBucket, CSELIB::DirEntryType* pDirEntry) override;
	WINCSEDEVICE_API bool listBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEDEVICE_API bool headObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) override;
	WINCSEDEVICE_API bool listObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEDEVICE_API bool listDisplayObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEDEVICE_API CSELIB::FILEIO_LENGTH_T getObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) override;
	WINCSEDEVICE_API bool putObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) override;
	WINCSEDEVICE_API bool deleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) override;
	WINCSEDEVICE_API bool deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) override;
};

}	// namespace CSEDVC

// EOF