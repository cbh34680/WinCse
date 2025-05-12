#pragma once

#include "CSDeviceBase.hpp"

namespace CSESS3
{

class CSDevice : public CSDeviceBase
{
private:
	WINCSESDKS3_API bool headObjectFromCache_(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);

public:
	CSDevice(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers)
		:
		CSDeviceBase(argIniSection, argWorkers)
	{
	}

	WINCSESDKS3_API ~CSDevice();

	WINCSESDKS3_API NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;

	WINCSESDKS3_API bool headBucket(CALLER_ARG const std::wstring& argBucket, CSELIB::DirEntryType* pDirEntry) override;
	WINCSESDKS3_API bool listBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSESDKS3_API bool headObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) override;
	WINCSESDKS3_API bool listObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSESDKS3_API bool listDisplayObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSESDKS3_API CSELIB::FILEIO_LENGTH_T getObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) override;
	WINCSESDKS3_API bool putObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) override;
	WINCSESDKS3_API bool deleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) override;
	WINCSESDKS3_API bool deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) override;
};

}	// namespace CSESS3

// EOF