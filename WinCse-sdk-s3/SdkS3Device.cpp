#include "SdkS3Device.hpp"
#include "ApiClient.hpp"

using namespace CSELIB;
using namespace CSEDVC;

namespace CSESS3 {

IApiClient* SdkS3Device::makeApiClient(RuntimeEnv* argRuntimeEnv, IWorker* argDelayedWorker)
{
	NEW_LOG_BLOCK();

	const auto clientRegion = this->getClientRegion();

	auto* s3Client = this->getS3Client();
	if (!s3Client)
	{
		errorW(L"fault: getS3Client");
		return nullptr;
	}

	return new ApiClient{ argRuntimeEnv, argDelayedWorker, clientRegion, s3Client };
}

}	// namespace CSESS3

// EOF