#include "CSDeviceInternal.h"
#include "IApiClient.hpp"

using namespace CSELIB;

namespace CSEDVC {

bool IApiClient::DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys)
{
	NEW_LOG_BLOCK();

	for (const auto& key: argKeys)
	{
		const auto optObjKey{ ObjectKey::fromObjectPath(argBucket, key) };
		if (optObjKey)
		{
			if (!this->DeleteObject(CONT_CALLER *optObjKey))
			{
				errorW(L"fault: DeleteObject optObjKey=%s", optObjKey->c_str());
				return false;
			}
		}
		else
		{
			errorW(L"fault: fromObjectPath argBucket=%s key=%s", argBucket.c_str(), key.c_str());
			return false;
		}

		traceW(L"success: DeleteObject optObjKey=%s", optObjKey->c_str());
	}

	return true;
}

}	// namespace CSEDVC

// EOF