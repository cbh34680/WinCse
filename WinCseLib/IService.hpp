#pragma once

namespace WinCseLib {

struct WINCSELIB_API IService
{
	virtual ~IService() = default;

	virtual bool OnSvcStart(const wchar_t* argWorkDir) = 0;
	virtual void OnSvcStop() = 0;
	virtual bool OnPostSvcStart() { return true; };
};

} // namespace WinCseLib

// EOF