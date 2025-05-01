#pragma once

#include "CSDeviceCommon.h"
#include "CSDeviceBase.hpp"

namespace CSEDAS3
{

class CSDevice final : public CSDeviceBase
{
private:
	// ディレクトリに特化

	CSELIB::DirInfoPtr makeDirInfoOfDir_1(const std::wstring& argFileName) const
	{
		return makeDirInfoOfDir(argFileName, mRuntimeEnv->DefaultCommonPrefixTime, mRuntimeEnv->DefaultFileAttributes);
	}

public:
	explicit CSDevice(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers)
		:
		CSDeviceBase(argIniSection, argWorkers)
	{
	}

	~CSDevice();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket, CSELIB::DirInfoPtr* pDirInfo) override;
	bool listBuckets(CALLER_ARG CSELIB::DirInfoPtrList* pDirInfoList) override;
	bool headObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtr* pDirInfo) override;
	bool listObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtrList* pDirInfoList) override;
	bool listDisplayObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirInfoPtrList* pDirInfoList) override;
	CSELIB::FILEIO_LENGTH_T getObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) override;
	bool putObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) override;
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