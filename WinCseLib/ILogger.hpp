#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace CSELIB {

struct ILogger
{
	virtual ~ILogger() = default;

	virtual PCWSTR getOutputDirectory() const = 0;

	virtual void writeToTraceLog(std::optional<std::wstring> argText) = 0;
	virtual void writeToErrorLog(std::optional<std::wstring> argText) = 0;

	virtual std::optional<std::wstring> makeTextW(int argIndent, PCWSTR argPath, int argLine, PCWSTR argFunc, DWORD argLastError, PCWSTR argFormat, ...) const = 0;
	virtual std::optional<std::wstring> makeTextA(int argIndent, PCSTR argPath, int argLine, PCSTR argFunc, DWORD argLastError, PCSTR argFormat, ...) const = 0;
};

WINCSELIB_API ILogger* CreateLogger(PCWSTR argTraceLogDir);
WINCSELIB_API ILogger* GetLogger();
WINCSELIB_API void DeleteLogger();

} // namespace CSELIB

#pragma warning(pop)
// EOF