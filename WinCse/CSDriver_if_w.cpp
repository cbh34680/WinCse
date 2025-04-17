#include "CSDriver.hpp"
#include <filesystem>


using namespace WCSE;


NTSTATUS CSDriver::canCreateObject(CALLER_ARG
	PCWSTR argFileName, bool argIsDir, ObjectKey* pObjKey) const noexcept
{
	NEW_LOG_BLOCK();

	// �ύX��̖��O�������Ώۂ��ǂ����m�F

	if (this->shouldIgnoreFileName(argFileName))
	{
		traceW(L"ignore pattern");
		return STATUS_OBJECT_NAME_INVALID;
	}

	auto fileObjKey{ ObjectKey::fromWinPath(argFileName) };
	traceW(L"fileObjKey=%s", fileObjKey.c_str());

	if (fileObjKey.invalid())
	{
		return STATUS_OBJECT_NAME_INVALID;
	}

	if (fileObjKey.isBucket())
	{
		// �o�P�b�g�ɑ΂��鑀��
		// 
		// "md \bucket\not\exist\yet\dir" �����s����ƃo�P�b�g�ɑ΂��� create ��
		// �Ăяo����邪�A����ɑ΂� STATUS_OBJECT_NAME_COLLISION �ȊO��ԋp�����
		// md �R�}���h�����s����

		traceW(L"not object: fileObjKey=%s", fileObjKey.c_str());

		if (mCSDevice->headBucket(CONT_CALLER fileObjKey.bucket(), nullptr))
		{
			//return STATUS_ACCESS_DENIED;
			//return FspNtStatusFromWin32(ERROR_ACCESS_DENIED);
			//return FspNtStatusFromWin32(ERROR_WRITE_PROTECT);
			//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);

			return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
		}
		else
		{
			return STATUS_ACCESS_DENIED;
		}
	}

	APP_ASSERT(fileObjKey.isObject());

	// �ύX��̖��O�����݂��邩�m�F

	auto objKey{ argIsDir ? fileObjKey.toDir() : std::move(fileObjKey) };
	APP_ASSERT(objKey.isObject());

	traceW(L"objKey=%s", objKey.c_str());

	if (mCSDevice->headObject(START_CALLER objKey, nullptr))
	{
		traceW(L"already exists: objKey=%s", objKey.c_str());

		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	// �t�@�C�����A�f�B���N�g�����𔽓]�������O�����݂��邩�m�F

	const auto chkObjKey{ argIsDir ? objKey.toFile() : objKey.toDir() };
	APP_ASSERT(chkObjKey.isObject());

	traceW(L"chkObjKey=%s", chkObjKey.c_str());

	if (mCSDevice->headObject(START_CALLER chkObjKey, nullptr))
	{
		traceW(L"already exists: chkObjKey=%s", chkObjKey.c_str());

		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	*pObjKey = std::move(objKey);

	return STATUS_SUCCESS;
}


#define RETURN_ERROR_IF_READONLY()		if (mReadOnly) { return STATUS_ACCESS_DENIED; }

NTSTATUS CSDriver::Create(PCWSTR FileName,
	UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoCreate);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');

	// CMD �� "echo word > file.txt" �Ƃ��ĐV�K�쐬����ꍇ
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, FileAttributes=%u, SecurityDescriptor=%p, AllocationSize=%llu, PFileContext=%p, FileInfo=%p",
		FileName, CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

	// �ꎞ�t�@�C���̂Ƃ��͋���
	// --> MS Office �Ȃǂ����ԃt�@�C������邱�ƂɑΉ�

	if (FA_MEANS_TEMPORARY(FileAttributes))
	{
		traceW(L"Deny opening temporary files");

		//return FspNtStatusFromWin32(ERROR_WRITE_PROTECT);
		return STATUS_ACCESS_DENIED;
	}

	// �쐬�ΏۂƓ����t�@�C���������݂��邩�`�F�b�N

	std::lock_guard lock_{ CreateNew.mGuard };

	traceW(L"check CreateNew=%s", FileName);

	const auto it = CreateNew.mFileInfos.find(FileName);
	if (it != CreateNew.mFileInfos.cend())
	{
		traceW(L"already exist: CreateNew=%s", FileName);
		return STATUS_OBJECT_NAME_COLLISION;
	}

	const bool isDir = CreateOptions & FILE_DIRECTORY_FILE;

	ObjectKey newObjKey;
	const auto ntstatus = this->canCreateObject(START_CALLER FileName, isDir, &newObjKey);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: canCreateObject");

		return ntstatus;
	}

	// create �����J�n

	// �V�K�쐬���̓���������݂̂� DoClose �܂ōs��

	auto fc{ std::unique_ptr<PTFS_FILE_CONTEXT, void(*)(void*)>((PTFS_FILE_CONTEXT*)calloc(1, sizeof(PTFS_FILE_CONTEXT)), free) };
	if (!fc)
	{
		traceW(L"fault: calloc");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto fn{ std::unique_ptr<wchar_t, void(*)(void*)>(_wcsdup(FileName), free) };
	if (!fn)
	{
		traceW(L"fault: _wcsdup");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PTFS_FILE_CONTEXT* FileContext = fc.get();
	FileContext->FileName = fn.get();

	// ���\�[�X���쐬�� UParam �ɕۑ�

	StatsIncr(_CallCreate);

	auto ctx{ std::unique_ptr<CSDeviceContext>{ mCSDevice->create(START_CALLER newObjKey, CreateOptions, GrantedAccess, FileAttributes) } };
	if (!ctx)
	{
		traceW(L"fault: create");

		//return STATUS_DEVICE_NOT_READY;
		return FspNtStatusFromWin32(ERROR_IO_DEVICE);
	}

	FileContext->FileInfo = ctx->mFileInfo;
	*FileInfo = ctx->mFileInfo;

	traceW(L"add CreateNew=%s", FileName);
	CreateNew.mFileInfos[FileName] = ctx->mFileInfo;

	ctx->mFlags |= CSDCTX_FLAGS_CREATE;

	FileContext->UParam = ctx.get();
	ctx.release();

	// �S�~����Ώۂɓo�^

	mResourceSweeper.add(FileContext);
	*PFileContext = FileContext;

	fn.release();
	fc.release();

	return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
//
// ���Ƀt�@�C�����J���Ă����Ԃ̂���
//

NTSTATUS CSDriver::SetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
	FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoSetFileSize);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	// �����炭�V�F���ō폜���삪�~�߂��Ă���
	RETURN_ERROR_IF_READONLY();

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

NTSTATUS CSDriver::Flush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
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
		traceW(L"fault: FlushFileBuffers");
		return FspNtStatusFromWin32(::GetLastError());
	}

	return GetFileInfoInternal(Handle, FileInfo);
}

// ---------------------------------------------------------------------------
//
// CreateFile() ���K�v�Ȃ���
//

NTSTATUS CSDriver::Overwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
	BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOverwrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	// ���v�����s��
	RETURN_ERROR_IF_READONLY();

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

NTSTATUS CSDriver::Write(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
	BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
	PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoWrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	// ���v�����s��
	RETURN_ERROR_IF_READONLY();

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

NTSTATUS CSDriver::SetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN argDeleteFile)
{
	StatsIncr(DoSetDelete);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	// �����炭�V�F���ō폜���삪�~�߂��Ă���
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, argDeleteFile=%s", FileName, BOOL_CSTRW(argDeleteFile));

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->mObjKey.valid());

	if (ctx->mObjKey.isBucket())
	{
		traceW(L"fault: delete bucket");
		return STATUS_ACCESS_DENIED;
	}

	return mCSDevice->setDelete(START_CALLER ctx, argDeleteFile);
}

NTSTATUS CSDriver::SetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 argFileAttributes,
	UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime,
	UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoSetBasicInfo);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);

	// �v���p�e�B�ő�����ύX�����ꍇ
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, FileAttributes=%u, CreationTime=%llu, LastAccessTime=%llu, LastWriteTime=%llu",
		FileContext->FileName, argFileAttributes, argCreationTime, argLastAccessTime, argLastWriteTime);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->mObjKey.valid());

	if (ctx->mObjKey.isBucket())
	{
		traceW(L"fault: setattr bucket");
		return STATUS_ACCESS_DENIED;
	}

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

NTSTATUS CSDriver::SetSecurity(PTFS_FILE_CONTEXT* FileContext,
	SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor)
{
	StatsIncr(DoSetSecurity);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && ModificationDescriptor);

	traceW(L"FileName=%s", FileContext->FileName);

	return STATUS_ACCESS_DENIED;
}

NTSTATUS CSDriver::Rename(PTFS_FILE_CONTEXT* FileContext,
	PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists)
{
	StatsIncr(DoRename);
	NEW_LOG_BLOCK();

	// CMD �� "ren old.txt new.txt" �����s����ƒʉ߂���
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, NewFileName=%s, ReplaceIfExists=%s",
		FileName, NewFileName, BOOL_CSTRW(ReplaceIfExists));

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);

	// �쐬�ΏۂƓ����t�@�C���������݂��邩�`�F�b�N

	ObjectKey newObjKey;
	auto ntstatus = this->canCreateObject(START_CALLER NewFileName, ctx->isDir(), &newObjKey);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: canCreateObject");

		return ntstatus;
	}

	// TODO: ��ŏ��� ... abort() �e�X�g�p
	//APP_ASSERT(newObjKey.key() != L"abort.wcse");

	// rename �����J�n

	ntstatus = mCSDevice->renameObject(START_CALLER ctx, newObjKey);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: renameObject");

		return ntstatus;
		//return STATUS_INVALID_DEVICE_REQUEST;
	}
	
	return STATUS_SUCCESS;
}

// EOF