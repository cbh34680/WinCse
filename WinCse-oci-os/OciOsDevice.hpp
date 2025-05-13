#pragma once

#include "CSDevice.hpp"

namespace CSEOOS
{

class OciOsDevice : public CSESS3::CSDevice
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
	OciOsDevice(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers);
	~OciOsDevice();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
};

}	// namespace CSEOOS


#ifdef WINCSEOCIOS_EXPORTS
#define WINCSEOCIOS_API __declspec(dllexport)
#else
#define WINCSEOCIOS_API __declspec(dllimport)
#endif

extern "C"
{
	WINCSEOCIOS_API CSELIB::ICSDevice* NewCSDevice(PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[]);
}

// EOF