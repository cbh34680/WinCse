#pragma once

#include "CSDevice.hpp"

namespace CSEAS3
{

class AwsS3Device : public CSESS3::CSDevice
{
private:
	std::unique_ptr<Aws::SDKOptions>	mSdkOptions;
	std::wstring						mRegion;
	std::unique_ptr<Aws::S3::S3Client>	mS3Client;

protected:
	std::wstring getClientRegion() override
	{
		return mRegion;
	}

	Aws::S3::S3Client* getClient() override
	{
		APP_ASSERT(mS3Client);

		return mS3Client.get();
	}

public:
	AwsS3Device(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers);
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