#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WCSE {

struct ILogger
{
	virtual ~ILogger() = default;
	virtual PCWSTR getOutputDirectory() = 0;
	virtual void traceA_impl(int indent, PCSTR, int, PCSTR, PCSTR format, ...) = 0;
	virtual void traceW_impl(int indent, PCWSTR, int, PCWSTR, PCWSTR format, ...) = 0;
};

} // namespace WCSE

#pragma warning(pop)
// EOF