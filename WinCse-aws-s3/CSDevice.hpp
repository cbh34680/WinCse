#pragma once

#include "CSDeviceCommon.h"
#include "CSDeviceBase.hpp"

namespace CSEDAS3
{

class CSDevice final : public CSDeviceBase
{
private:
	bool headObjectFromCache_(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);

public:
	explicit CSDevice(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers)
		:
		CSDeviceBase(argIniSection, argWorkers)
	{
	}

	~CSDevice();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket, CSELIB::DirEntryType* pDirEntry) override;
	bool listBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) override;
	bool headObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) override;
	bool listObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	bool listDisplayObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	CSELIB::FILEIO_LENGTH_T getObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) override;
	bool putObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) override;
	bool deleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) override;
	bool deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) override;
};

}	// namespace CSEDAS3

#ifdef WINCSEAWSS3_EXPORTS
#define AWSS3_API __declspec(dllexport)
#else
#define AWSS3_API __declspec(dllimport)
#endif

extern "C"
{
	AWSS3_API CSELIB::ICSDevice* NewCSDevice(PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[]);
}

// EOF