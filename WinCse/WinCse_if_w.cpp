#include "WinCseLib.h"
#include "WinCse.hpp"

using namespace WinCseLib;


VOID WinCse::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);
	APP_ASSERT(FileName);

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"(FileContext)FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Flags=%lu", Flags);

	mCSDevice->cleanup(START_CALLER (IOpenContext*)FileContext->UParam, Flags);
}

NTSTATUS WinCse::DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN argDeleteFile)
{
	StatsIncr(DoSetDelete);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);
	APP_ASSERT(FileName);

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"(FileContext)FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"deleteFile=%s", argDeleteFile ? L"true" : L"false");

	return mCSDevice->remove(START_CALLER (IOpenContext*)FileContext->UParam, argDeleteFile);
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

// EOF