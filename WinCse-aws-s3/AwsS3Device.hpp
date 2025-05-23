#pragma once

#include "SdkS3Device.hpp"

#ifdef WINCSEAWSS3_EXPORTS
#define WINCSEAWSS3_API __declspec(dllexport)
#else
#define WINCSEAWSS3_API __declspec(dllimport)
#endif

namespace CSEAS3
{

class AwsS3Device : public CSESS3::SdkS3Device
{
private:
	std::unique_ptr<Aws::SDKOptions>	mSdkOptions;
	std::wstring						mClientRegion;
	std::unique_ptr<Aws::S3::S3Client>	mS3Client;

protected:
	std::wstring getClientRegion() override
	{
		return mClientRegion;
	}

	Aws::S3::S3Client* getS3Client() override
	{
		return mS3Client.get();
	}

public:
	using CSESS3::SdkS3Device::SdkS3Device;
	~AwsS3Device();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
};

}	// namespace CSEAS3

extern "C"
{

WINCSEAWSS3_API CSELIB::ICSDevice* NewCSDevice(PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[]);

}

// EOF