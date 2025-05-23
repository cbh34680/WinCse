#pragma once

#include "WinCseLib.h"

#ifdef WINCSEDEVICE_EXPORTS
#define WINCSEDEVICE_API __declspec(dllexport)
#else
#define WINCSEDEVICE_API __declspec(dllimport)
#endif

namespace CSEDVC
{

}	// namespace CSEDVC


// EOF