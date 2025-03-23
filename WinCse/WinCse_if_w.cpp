#include "WinCseLib.h"
#include "WinCse.hpp"

using namespace WinCseLib;


VOID WinCse::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"(FileContext)FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Flags=%lu", Flags);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;

	mCSDevice->cleanup(START_CALLER ctx, Flags);
}

NTSTATUS WinCse::DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN argDeleteFile)
{
	StatsIncr(DoSetDelete);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);
	APP_ASSERT(!mReadonlyVolume);			// おそらくシェルで削除操作が止められている

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"(FileContext)FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"deleteFile=%s", BOOL_CSTRW(argDeleteFile));

	return mCSDevice->remove(START_CALLER (CSDeviceContext*)FileContext->UParam, argDeleteFile);
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

NTSTATUS WinCse::DoSetBasicInfo(PTFS_FILE_CONTEXT* FileContext, const UINT32 argFileAttributes,
	const UINT64 argCreationTime, const UINT64 argLastAccessTime, const UINT64 argLastWriteTime,
	const UINT64 argChangeTime, FSP_FSCTL_FILE_INFO *FileInfo)
{
	StatsIncr(DoSetBasicInfo);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);

	traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

#if 0
	UINT32 fileAttributes = argFileAttributes;
	UINT64 creationTime = argCreationTime;
	UINT64 lastAccessTime = argLastAccessTime;
	UINT64 lastWriteTime = argLastWriteTime;
	//UINT64 changeTime = argChangeTime;

	auto Handle = ctx->mFile.handle();

	FILE_BASIC_INFO BasicInfo{};

	if (INVALID_FILE_ATTRIBUTES == fileAttributes)
	{
		fileAttributes = 0;
	}
	else if (0 == fileAttributes)
	{
		fileAttributes = FILE_ATTRIBUTE_NORMAL;
	}

	BasicInfo.FileAttributes = fileAttributes;
	BasicInfo.CreationTime.QuadPart = creationTime;
	BasicInfo.LastAccessTime.QuadPart = lastAccessTime;
	BasicInfo.LastWriteTime.QuadPart = lastWriteTime;
	//BasicInfo.ChangeTime = changeTime;

	if (!::SetFileInformationByHandle(Handle,
		FileBasicInfo, &BasicInfo, sizeof BasicInfo))
	{
		return FspNtStatusFromWin32(::GetLastError());
	}

	return GetFileInfoInternal(Handle, FileInfo);

#else
	// タイムスタンプの更新はしない

	*FileInfo = ctx->mFileInfo;

	return STATUS_SUCCESS;
#endif
}

NTSTATUS WinCse::DoSetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
	FSP_FSCTL_FILE_INFO *FileInfo)
{
	StatsIncr(DoSetFileSize);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIR(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;

	APP_ASSERT(ctx);
	APP_ASSERT(ctx->isFile());

	traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

	auto Handle = ctx->mFile.handle();

	FILE_ALLOCATION_INFO AllocationInfo{};
	FILE_END_OF_FILE_INFO EndOfFileInfo{};

	if (SetAllocationSize)
	{
		/*
		* This file system does not maintain AllocationSize, although NTFS clearly can.
		* However it must always be FileSize <= AllocationSize and NTFS will make sure
		* to truncate the FileSize if it sees an AllocationSize < FileSize.
		*
		* If OTOH a very large AllocationSize is passed, the call below will increase
		* the AllocationSize of the underlying file, although our file system does not
		* expose this fact. This AllocationSize is only temporary as NTFS will reset
		* the AllocationSize of the underlying file when it is closed.
		*/

		AllocationInfo.AllocationSize.QuadPart = NewSize;

		if (!::SetFileInformationByHandle(Handle,
			FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
		{
			return FspNtStatusFromWin32(::GetLastError());
		}
	}
	else
	{
		EndOfFileInfo.EndOfFile.QuadPart = NewSize;

		if (!::SetFileInformationByHandle(Handle,
			FileEndOfFileInfo, &EndOfFileInfo, sizeof EndOfFileInfo))
		{
			return FspNtStatusFromWin32(::GetLastError());
		}
	}

	return GetFileInfoInternal(Handle, FileInfo);
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

NTSTATUS WinCse::DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
	BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
	PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo)
{
	StatsIncr(DoWrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred && FileInfo);
	APP_ASSERT(!FA_IS_DIR(FileContext->FileInfo.FileAttributes));		// ファイルのみ
	//APP_ASSERT(Offset <= FileContext->FileInfo.FileSize);

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Size=%llu Offset=%llu", FileContext->FileInfo.FileSize, Offset);

	return mCSDevice->writeObject(START_CALLER (CSDeviceContext*)FileContext->UParam,
		Buffer, Offset, Length, WriteToEndOfFile, ConstrainedIo, PBytesTransferred, FileInfo);
}

// EOF