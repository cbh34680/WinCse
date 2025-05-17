#pragma once

#include "CSDevice.hpp"
#include "aws_sdk_s3_client.h"

#ifdef WINCSESDKS3_EXPORTS
#define WINCSESDKS3_API __declspec(dllexport)
#else
#define WINCSESDKS3_API __declspec(dllimport)
#endif

namespace CSESS3
{

}	// namespace CSESS3

// EOF