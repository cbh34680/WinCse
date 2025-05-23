#pragma once

#include "CSDevice.hpp"
#include "gcp_sdk_gs.h"

#ifdef WINCSEGCPGS_EXPORTS
#define WINCSEGCPGS_API __declspec(dllexport)
#else
#define WINCSEGCPGS_API __declspec(dllimport)
#endif

namespace CSEGGS
{

}	// namespace CSEGGS

	// EOF