#pragma once

#include "SdkS3Common.h"

namespace CSESS3
{

class SdkS3Device : public CSEDVC::CSDevice
{
protected:
	virtual std::wstring getClientRegion() = 0;
	virtual Aws::S3::S3Client* getS3Client() = 0;

	WINCSESDKS3_API CSEDVC::IApiClient* newApiClient(CSEDVC::RuntimeEnv* argRuntimeEnv, CSELIB::IWorker* argDelayedWorker) override;

public:
	using CSEDVC::CSDevice::CSDevice;
};

}	// namespace CSESS3

// EOF