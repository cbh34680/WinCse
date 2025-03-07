#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WinCseLib {

struct WINCSELIB_API ILogger
{
	virtual ~ILogger() = default;

	virtual const wchar_t* getOutputDirectory() = 0;

	virtual void traceA_impl(const int indent, const char*, const int, const char*, const char* format, ...) = 0;
	virtual void traceW_impl(const int indent, const wchar_t*, const int, const wchar_t*, const wchar_t* format, ...) = 0;
};

} // namespace WinCseLib

#pragma warning(pop)
// EOF