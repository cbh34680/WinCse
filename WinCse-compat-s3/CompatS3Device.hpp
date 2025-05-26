#pragma once

#include "CSDevice.hpp"
#include "SdkS3Client.hpp"

namespace CSECS3
{

class CompatS3Device : public CSEDVC::CSDevice
{
private:
	std::unique_ptr<Aws::SDKOptions>	mSdkOptions;
	std::wstring						mClientRegion;
	std::unique_ptr<Aws::S3::S3Client>	mS3Client;
	bool								mIgnoreRegionDifferences = false;

protected:
	CSEDVC::IApiClient* newApiClient(CSEDVC::RuntimeEnv* argRuntimeEnv, CSELIB::IWorker* argDelayedWorker) override
	{
		return new CSESS3::SdkS3Client{ argRuntimeEnv, argDelayedWorker, mClientRegion, mS3Client.get() };
	}

public:
	using CSDevice::CSDevice;
	~CompatS3Device();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
};

}	// namespace CSECS3

#ifdef WINCSECOMPATS3_EXPORTS
#define WINCSECOMPATS3_API __declspec(dllexport)
#else
#define WINCSECOMPATS3_API __declspec(dllimport)
#endif

extern "C"
{

WINCSECOMPATS3_API CSELIB::ICSDevice* NewCSDevice(PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[]);

}

// EOF