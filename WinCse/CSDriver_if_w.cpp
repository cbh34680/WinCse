#include "WinCseLib.h"
#include "CSDriver.hpp"
#include <filesystem>


using namespace WCSE;

// ---------------------------------------------------------------------------
//
// ���Ƀt�@�C�����J���Ă����Ԃ̂���
//

NTSTATUS CSDriver::DoSetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
	FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoSetFileSize);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	traceW(L"FileName=%s, NewSize=%llu, SetAllocationSize=%s", FileContext->FileName, NewSize, BOOL_CSTRW(SetAllocationSize));

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->isFile());
	APP_ASSERT(ctx->mFile.valid());

	HANDLE Handle = ctx->mFile.handle();
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

	ctx->mFlags |= CSDCTX_FLAGS_SET_FILE_SIZE;

	return GetFileInfoInternal(Handle, FileInfo);
}

NTSTATUS CSDriver::DoFlush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoFlush);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	traceW(L"FileName=%s", FileContext->FileName);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->isFile());
	APP_ASSERT(ctx->mFile.valid());

	auto Handle = ctx->mFile.handle();
	APP_ASSERT(Handle != INVALID_HANDLE_VALUE);

	/* we do not flush the whole volume, so just return SUCCESS */
	if (0 == Handle)
	{
		return STATUS_SUCCESS;
	}

	if (!::FlushFileBuffers(Handle))
	{
		return FspNtStatusFromWin32(::GetLastError());
	}

	return GetFileInfoInternal(Handle, FileInfo);
}

// ---------------------------------------------------------------------------
//
// CreateFile() ���K�v�Ȃ���
//

NTSTATUS CSDriver::DoOverwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
	BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOverwrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	traceW(L"FileName=%s, FileAttributes=%u, ReplaceFileAttributes=%s, AllocationSize=%llu",
		FileContext->FileName, FileAttributes, BOOL_CSTRW(ReplaceFileAttributes), AllocationSize);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->isFile());

	// ���[�J���ɃL���b�V�������݂��Ȃ��ꍇ������̂ŁADoDelete() �Ɠ�����
	// ���Ƃ͈قȂ�A�����ł� CREATE_ALWAYS �ɂȂ�
	//
	// --> �㏑���Ȃ̂ŁA�����f�[�^���ӎ�����K�v���Ȃ�

	HANDLE Handle = INVALID_HANDLE_VALUE;
	NTSTATUS ntstatus = mCSDevice->getHandleFromContext(START_CALLER ctx, 0, CREATE_ALWAYS, &Handle);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: getHandleFromContext");
		return ntstatus;
	}

	FILE_BASIC_INFO BasicInfo{};
	FILE_ALLOCATION_INFO AllocationInfo{};
	FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo{};

	if (ReplaceFileAttributes)
	{
		if (0 == FileAttributes)
		{
			FileAttributes = FILE_ATTRIBUTE_NORMAL;
		}

		BasicInfo.FileAttributes = FileAttributes;

		if (!::SetFileInformationByHandle(Handle,
			FileBasicInfo, &BasicInfo, sizeof BasicInfo))
		{
			return FspNtStatusFromWin32(::GetLastError());
		}
	}
	else if (0 != FileAttributes)
	{
		if (!::GetFileInformationByHandleEx(Handle,
			FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
		{
			return FspNtStatusFromWin32(::GetLastError());
		}

		BasicInfo.FileAttributes = FileAttributes | AttributeTagInfo.FileAttributes;
		if (BasicInfo.FileAttributes ^ FileAttributes)
		{
			if (!::SetFileInformationByHandle(Handle,
				FileBasicInfo, &BasicInfo, sizeof BasicInfo))
			{
				return FspNtStatusFromWin32(::GetLastError());
			}
		}
	}

	if (!::SetFileInformationByHandle(Handle,
		FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
	{
		return FspNtStatusFromWin32(::GetLastError());
	}

	ctx->mFlags |= CSDCTX_FLAGS_OVERWRITE;

	return GetFileInfoInternal(Handle, FileInfo);
}

NTSTATUS CSDriver::DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
	BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
	PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoWrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	traceW(L"FileName=%s, FileAttributes=%u, FileSize=%llu, Offset=%llu, Length=%lu, WriteToEndOfFile=%s, ConstrainedIo=%s",
		FileContext->FileName, FileContext->FileInfo.FileAttributes, FileContext->FileInfo.FileSize,
		Offset, Length, BOOL_CSTRW(WriteToEndOfFile), BOOL_CSTRW(ConstrainedIo));

	return mCSDevice->writeObject(START_CALLER (CSDeviceContext*)FileContext->UParam,
		Buffer, Offset, Length, WriteToEndOfFile, ConstrainedIo, PBytesTransferred, FileInfo);
}

// ---------------------------------------------------------------------------
//
// �ȍ~�̓t�@�C���ƃf�B���N�g���̗������Ώ�
//

NTSTATUS CSDriver::DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN argDeleteFile)
{
	StatsIncr(DoSetDelete);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);
	APP_ASSERT(!mReadonlyVolume);			// �����炭�V�F���ō폜���삪�~�߂��Ă���

	traceW(L"FileName=%s, argDeleteFile=%s", FileName, BOOL_CSTRW(argDeleteFile));

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->mObjKey.isObject());

	if (ctx->isDir())
	{
		DirInfoListType dirInfoList;

		if (!mCSDevice->listObjects(START_CALLER ctx->mObjKey, &dirInfoList))
		{
			traceW(L"fault: listObjects");
			return STATUS_OBJECT_NAME_INVALID;
		}

		const auto it = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
		{
			return wcscmp(dirInfo->FileNameBuf, L".") != 0
				&& wcscmp(dirInfo->FileNameBuf, L"..") != 0
				&& FA_IS_DIRECTORY(dirInfo->FileInfo.FileAttributes);
		});

		if (it != dirInfoList.cend())
		{
			// �T�u�f�B���N�g��������ꍇ�͍폜�s��

			traceW(L"dir not empty");
			return STATUS_CANNOT_DELETE;
			//return STATUS_DIRECTORY_NOT_EMPTY;
		}
	}
	else if (ctx->isFile())
	{
		// �L���b�V���E�t�@�C�����폜
		// 
		// remove() �ȂǂŒ��ڍ폜����̂ł͂Ȃ��A�폜�t���O��ݒ肵���t�@�C�����쐬��
		// �����ɊJ����Ă���t�@�C�������݂��Ȃ��Ȃ�����A�����I�ɍ폜�����悤�ɂ���
		//
		// ���̂��߁A�L���b�V���E�t�@�C�������݂��Ȃ��ꍇ�͍쐬���Ȃ����
		// �Ȃ�Ȃ��̂ŁA���Ƃ͈قȂ� OPEN_ALWAYS �ɂȂ��Ă���

		HANDLE Handle = INVALID_HANDLE_VALUE;
		NTSTATUS ntstatus = mCSDevice->getHandleFromContext(START_CALLER ctx, 0, OPEN_ALWAYS, &Handle);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: getHandleFromContext");
			return ntstatus;
		}

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

NTSTATUS CSDriver::DoSetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 argFileAttributes,
	UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime,
	UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoSetBasicInfo);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);

	traceW(L"FileName=%s, FileAttributes=%u, CreationTime=%llu, LastAccessTime=%llu, LastWriteTime=%llu",
		FileContext->FileName, argFileAttributes, argCreationTime, argLastAccessTime, argLastWriteTime);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->mObjKey.valid());

	if (ctx->isFile())
	{
		// ���[�J���ɃL���b�V�������݂��Ă���ꍇ�̂ݑ�����ύX����
		//
		// --> robocopy /COPY:T �΍�

		HANDLE Handle = INVALID_HANDLE_VALUE;

		auto ntstatus = mCSDevice->getHandleFromContext(START_CALLER ctx, 0, OPEN_EXISTING, &Handle);
		if (NT_SUCCESS(ntstatus))
		{
			UINT32 FileAttributes = argFileAttributes;

			FILE_BASIC_INFO BasicInfo{};

			if (INVALID_FILE_ATTRIBUTES == FileAttributes)
				FileAttributes = 0;
			else if (0 == FileAttributes)
				FileAttributes = FILE_ATTRIBUTE_NORMAL;

			//BasicInfo.FileAttributes = FileAttributes;
			BasicInfo.CreationTime.QuadPart = argCreationTime;
			BasicInfo.LastAccessTime.QuadPart = argLastAccessTime;
			BasicInfo.LastWriteTime.QuadPart = argLastWriteTime;
			//BasicInfo.ChangeTime.QuadPart = argChangeTime;

			if (!::SetFileInformationByHandle(Handle,
				FileBasicInfo, &BasicInfo, sizeof BasicInfo))
				return FspNtStatusFromWin32(::GetLastError());

			ntstatus = GetFileInfoInternal(Handle, FileInfo);
			if (!NT_SUCCESS(ntstatus))
			{
				traceW(L"fault: GetFileInfoInternal");
				return ntstatus;
			}

			ctx->mFlags |= CSDCTX_FLAGS_SET_BASIC_INFO;

			return STATUS_SUCCESS;
		}
		else
		{
			if (ntstatus != STATUS_OBJECT_NAME_NOT_FOUND)
			{
				traceW(L"fault: getHandleFromContext");
				return ntstatus;
			}
		}
	}

	// �{���� STATUS_INVALID_DEVICE_REQUEST �Ƃ��������Arobocopy �����s����̂�
	// �S�Ė�������

	const bool isDir = FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes);
	const HANDLE Handle = isDir ? mRefDir.handle() : mRefFile.handle();

	return GetFileInfoInternal(Handle, FileInfo);
}

NTSTATUS CSDriver::DoSetSecurity(PTFS_FILE_CONTEXT* FileContext,
	SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor)
{
	StatsIncr(DoSetSecurity);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && ModificationDescriptor);

	traceW(L"FileName=%s", FileContext->FileName);

	return STATUS_ACCESS_DENIED;
	//return STATUS_INVALID_DEVICE_REQUEST;
	//return STATUS_SUCCESS;
}

NTSTATUS CSDriver::DoRename(PTFS_FILE_CONTEXT* FileContext,
	PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists)
{
	StatsIncr(DoRename);
	NEW_LOG_BLOCK();

	traceW(L"FileName=%s, NewFileName=%s, ReplaceIfExists=%s",
		FileName, NewFileName, BOOL_CSTRW(ReplaceIfExists));

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->mObjKey.valid());

	// �ύX��̖��O�����݂��邩�m�F

	auto newFileKey{ ObjectKey::fromWinPath(NewFileName) };
	APP_ASSERT(newFileKey.isObject());

	const auto newObjKey{ ctx->isDir() ? newFileKey.toDir() : std::move(newFileKey) };
	APP_ASSERT(newObjKey.isObject());

	traceW(L"newObjKey=%s", newObjKey.c_str());

	if (mCSDevice->headObject(START_CALLER newObjKey, nullptr))
	{
		traceW(L"already exists: newObjKey=%s", newObjKey.c_str());

		return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
	}

	// �t�@�C�����A�f�B���N�g�����𔽓]�������O�����݂��邩�m�F

	const auto chkObjKey{ ctx->isDir() ? newObjKey.toFile() : newObjKey.toDir() };

	traceW(L"chkObjKey=%s", chkObjKey.c_str());

	if (mCSDevice->headObject(START_CALLER chkObjKey, nullptr))
	{
		traceW(L"already exists: chkObjKey=%s", chkObjKey.c_str());

		return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
	}

	// ���l�[�������̎��s

	if (!mCSDevice->renameObject(START_CALLER ctx, newObjKey))
	{
		traceW(L"fault: renameObject");

		return STATUS_INVALID_DEVICE_REQUEST;
	}
	
	return STATUS_SUCCESS;
}

// EOF