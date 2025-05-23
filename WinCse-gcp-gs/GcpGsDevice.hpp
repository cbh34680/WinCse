#pragma once

#include "GcpGsCommon.h"

namespace CSEGGS
{

class GcpGsDevice : public CSEDVC::CSDevice
{
private:
	std::wstring mProjectId;

protected:
	WINCSEGCPGS_API CSEDVC::IApiClient* newApiClient(CSEDVC::RuntimeEnv* argRuntimeEnv, CSELIB::IWorker* argDelayedWorker) override;

public:
	using CSEDVC::CSDevice::CSDevice;

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
};

}	// namespace CSEGGS

extern "C"
{

WINCSEGCPGS_API CSELIB::ICSDevice* NewCSDevice(PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[]);

}

// EOF