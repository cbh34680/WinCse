#include "WinCseLib.h"
#include "WinCse.hpp"

using namespace WinCseLib;

#undef traceA



NTSTATUS WinCse::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoCreate()
{
	StatsIncr(DoCreate);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoFlush()
{
	StatsIncr(DoFlush);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoOverwrite()
{
	StatsIncr(DoOverwrite);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoRename()
{
	StatsIncr(DoRename);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoSetBasicInfo()
{
	StatsIncr(DoSetBasicInfo);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoSetFileSize()
{
	StatsIncr(DoSetFileSize);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoSetPath()
{
	StatsIncr(DoSetPath);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoSetSecurity()
{
	StatsIncr(DoSetSecurity);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoWrite()
{
	StatsIncr(DoWrite);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile)
{
	StatsIncr(DoSetDelete);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

// EOF