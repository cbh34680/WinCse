#include "SdkS3Device.hpp"
#include "SdkS3Client.hpp"

using namespace CSELIB;
using namespace CSEDVC;

namespace CSESS3 {

IApiClient* SdkS3Device::newApiClient(RuntimeEnv* argRuntimeEnv, IWorker* argDelayedWorker)
{
	NEW_LOG_BLOCK();

	auto* s3Client = this->getS3Client();
	if (!s3Client)
	{
		errorW(L"fault: getS3Client");
		return nullptr;
	}

	return new SdkS3Client{ argRuntimeEnv, argDelayedWorker, this->getClientRegion(), s3Client };
}

}	// namespace CSESS3

// EOF