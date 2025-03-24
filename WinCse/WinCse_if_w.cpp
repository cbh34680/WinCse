#include "WinCseLib.h"
#include "WinCse.hpp"

using namespace WinCseLib;


NTSTATUS WinCse::DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
	BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
	PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo)
{
	StatsIncr(DoWrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred && FileInfo);
	APP_ASSERT(!FA_IS_DIR(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Size=%llu Offset=%llu", FileContext->FileInfo.FileSize, Offset);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	auto Handle = mCSDevice->HandleFromContext(START_CALLER ctx);

	if (ConstrainedIo)
	{
		LARGE_INTEGER FileSize;

		if (!::GetFileSizeEx(Handle, &FileSize))
		{
			const auto lerr = ::GetLastError();
			traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

			return FspNtStatusFromWin32(lerr);
		}

		if (Offset >= (UINT64)FileSize.QuadPart)
		{
			return STATUS_SUCCESS;
		}

		if (Offset + Length > (UINT64)FileSize.QuadPart)
		{
			Length = (ULONG)((UINT64)FileSize.QuadPart - Offset);
		}
	}

	OVERLAPPED Overlapped{};

	Overlapped.Offset = (DWORD)Offset;
	Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

	if (!::WriteFile(Handle, Buffer, Length, PBytesTransferred, &Overlapped))
	{
		const auto lerr = ::GetLastError();
		traceW(L"fault: WriteFile lerr=%lu", lerr);

		return FspNtStatusFromWin32(lerr);
	}

	ctx->mWrite = true;

	return GetFileInfoInternal(Handle, FileInfo);
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

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;

	traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

	if (!ctx->mObjKey.hasKey())
	{
		traceW(L"fault: delete bucket");
		return STATUS_OBJECT_NAME_INVALID;
	}

	if (ctx->isDir())
	{
		DirInfoListType dirInfoList;

		if (!mCSDevice->listObjects(START_CALLER ctx->mObjKey, &dirInfoList))
		{
			traceW(L"fault: listObjects");
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

#if 0
		const auto it = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
		{
			return wcscmp(dirInfo->FileNameBuf, L".") != 0 && wcscmp(dirInfo->FileNameBuf, L"..") != 0;
		});

		if (it != dirInfoList.end())
		{
			// 空でないディレクトリは削除不可
			// --> ".", ".." 以外のファイル/ディレクトリが存在する

			traceW(L"dir not empty");
			return STATUS_CANNOT_DELETE;
		}
#else
		const auto it = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
		{
			return wcscmp(dirInfo->FileNameBuf, L".") != 0
				&& wcscmp(dirInfo->FileNameBuf, L"..") != 0
				&& FA_IS_DIR(dirInfo->FileInfo.FileAttributes);
		});

		if (it != dirInfoList.end())
		{
			// サブディレクトリを持つディレクトリは削除不可
			// 
			// --> ".", ".." 以外のディレクトリが存在するもの

			traceW(L"dir not empty");
			return STATUS_CANNOT_DELETE;
		}

#endif
	}
	else if (ctx->isFile())
	{
		// キャッシュ・ファイルを削除
		// 
		// --> remove() などで直接削除するのではなく、削除フラグを設定したファイルを作成し
		//     同時に開かれているファイルが存在しなくなったら、自動的に削除されるようにする

		auto Handle = mCSDevice->HandleFromContext(START_CALLER ctx);
		APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

		FILE_DISPOSITION_INFO DispositionInfo{};
		DispositionInfo.DeleteFile = argDeleteFile;

		if (!::SetFileInformationByHandle(Handle,
			FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
		{
			const auto lerr = ::GetLastError();
			traceW(L"fault: SetFileInformationByHandle lerr=%lu", lerr);

			return FspNtStatusFromWin32(lerr);
		}

		traceW(L"success: SetFileInformationByHandle(DeleteFile=%s)", BOOL_CSTRW(argDeleteFile));
	}
	else
	{
		APP_ASSERT(0);
	}

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::DoOverwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
	BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo)
{
	StatsIncr(DoOverwrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIR(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);

	auto Handle = mCSDevice->HandleFromContext(START_CALLER (CSDeviceContext*)FileContext->UParam);
	APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

	FILE_BASIC_INFO BasicInfo{};
	FILE_ALLOCATION_INFO AllocationInfo{};
	FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo{};

	if (ReplaceFileAttributes)
	{
		if (0 == FileAttributes)
			FileAttributes = FILE_ATTRIBUTE_NORMAL;

		BasicInfo.FileAttributes = FileAttributes;
		if (!::SetFileInformationByHandle(Handle,
			FileBasicInfo, &BasicInfo, sizeof BasicInfo))
			return FspNtStatusFromWin32(::GetLastError());
	}
	else if (0 != FileAttributes)
	{
		if (!::GetFileInformationByHandleEx(Handle,
			FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
			return FspNtStatusFromWin32(::GetLastError());

		BasicInfo.FileAttributes = FileAttributes | AttributeTagInfo.FileAttributes;
		if (BasicInfo.FileAttributes ^ FileAttributes)
		{
			if (!::SetFileInformationByHandle(Handle,
				FileBasicInfo, &BasicInfo, sizeof BasicInfo))
				return FspNtStatusFromWin32(::GetLastError());
		}
	}

	if (!::SetFileInformationByHandle(Handle,
		FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
		return FspNtStatusFromWin32(::GetLastError());

	return GetFileInfoInternal(Handle, FileInfo);
}

NTSTATUS WinCse::DoFlush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO *FileInfo)
{
	StatsIncr(DoFlush);
	NEW_LOG_BLOCK();

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

	auto Handle = ctx->mFile.handle();
	APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

	/* we do not flush the whole volume, so just return SUCCESS */
	if (0 == Handle)
		return STATUS_SUCCESS;

	if (!::FlushFileBuffers(Handle))
		return FspNtStatusFromWin32(::GetLastError());

	return GetFileInfoInternal(Handle, FileInfo);
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

	UINT32 fileAttributes = argFileAttributes;
	UINT64 creationTime = argCreationTime;
	UINT64 lastAccessTime = argLastAccessTime;
	UINT64 lastWriteTime = argLastWriteTime;
	//UINT64 changeTime = argChangeTime;

	auto Handle = mCSDevice->HandleFromContext(START_CALLER (CSDeviceContext*)FileContext->UParam);
	APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

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
	APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

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

NTSTATUS WinCse::DoSetSecurity(PTFS_FILE_CONTEXT* FileContext,
	SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor)
{
	StatsIncr(DoSetSecurity);
	NEW_LOG_BLOCK();

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->isFile());

	auto Handle = ctx->mFile.handle();
	APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

	if (!::SetKernelObjectSecurity(Handle, SecurityInformation, ModificationDescriptor))
		return FspNtStatusFromWin32(::GetLastError());

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::DoRename(PTFS_FILE_CONTEXT* FileContext,
	PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists)
{
	StatsIncr(DoRename);

	NEW_LOG_BLOCK();

	return STATUS_INVALID_DEVICE_REQUEST;
}

// EOF