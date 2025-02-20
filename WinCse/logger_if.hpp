#pragma once

struct ILogger
{
	virtual ~ILogger() = 0;

	virtual void traceA_impl(const int indent, const char*, const int, const char*, const char* format, ...) = 0;
	virtual void traceW_impl(const int indent, const wchar_t*, const int, const wchar_t*, const wchar_t* format, ...) = 0;
};
