#pragma once

#include "WinCseLib.h"

namespace CSEDAS3
{

CSELIB::DirInfoPtr makeDirInfoOfDir(const std::wstring& argFileName, CSELIB::FILETIME_100NS_T argFileTime100ns, UINT32 argFileAttributes);

}	// namespace CSEDAS3

// EOF