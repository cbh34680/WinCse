#pragma once

#include "CSDevice.hpp"
#include "SdkS3Client.hpp"

namespace CSEAS3
{

class AwsS3Device : public CSEDVC::CSDevice
{
private:
	std::unique_ptr<Aws::SDKOptions>	mSdkOptions;
	std::wstring						mClientRegion;
	std::unique_ptr<Aws::S3::S3Client>	mS3Client;

protected:
	CSEDVC::IApiClient* newApiClient(CSEDVC::RuntimeEnv* argRuntimeEnv, CSELIB::IWorker* argDelayedWorker) override
	{
		return new CSESS3::SdkS3Client{ argRuntimeEnv, argDelayedWorker, mClientRegion, mS3Client.get() };
	}

public:
	using CSDevice::CSDevice;
	~AwsS3Device();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
};

}	// namespace CSEAS3

#ifdef WINCSEAWSS3_EXPORTS
#define WINCSEAWSS3_API __declspec(dllexport)
#else
#define WINCSEAWSS3_API __declspec(dllimport)
#endif

extern "C"
{

WINCSEAWSS3_API CSELIB::ICSDevice* NewCSDevice(PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[]);

}

// EOF