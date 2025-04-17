#include "CSDriver.hpp"
#include <filesystem>


using namespace WCSE;


NTSTATUS CSDriver::canCreateObject(CALLER_ARG
	PCWSTR argFileName, bool argIsDir, ObjectKey* pObjKey) const noexcept
{
	NEW_LOG_BLOCK();

	// 変更後の名前が無視対象かどうか確認

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
		// バケットに対する操作
		// 
		// "md \bucket\not\exist\yet\dir" を実行するとバケットに対して create が
		// 呼び出されるが、これに対し STATUS_OBJECT_NAME_COLLISION 以外を返却すると
		// md コマンドが失敗する

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

	// 変更先の名前が存在するか確認

	auto objKey{ argIsDir ? fileObjKey.toDir() : std::move(fileObjKey) };
	APP_ASSERT(objKey.isObject());

	traceW(L"objKey=%s", objKey.c_str());

	if (mCSDevice->headObject(START_CALLER objKey, nullptr))
	{
		traceW(L"already exists: objKey=%s", objKey.c_str());

		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	// ファイル名、ディレクトリ名を反転させ名前が存在するか確認

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

	// CMD で "echo word > file.txt" として新規作成する場合
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, FileAttributes=%u, SecurityDescriptor=%p, AllocationSize=%llu, PFileContext=%p, FileInfo=%p",
		FileName, CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

	// 一時ファイルのときは拒否
	// --> MS Office などが中間ファイルを作ることに対応

	if (FA_MEANS_TEMPORARY(FileAttributes))
	{
		traceW(L"Deny opening temporary files");

		//return FspNtStatusFromWin32(ERROR_WRITE_PROTECT);
		return STATUS_ACCESS_DENIED;
	}

	// 作成対象と同じファイル名が存在するかチェック

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

	// create 処理開始

	// 新規作成時はメモリ操作のみで DoClose まで行く

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

	// リソースを作成し UParam に保存

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

	// ゴミ回収対象に登録

	mResourceSweeper.add(FileContext);
	*PFileContext = FileContext;

	fn.release();
	fc.release();

	return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
//
// 既にファイルが開いている状態のもの
//

NTSTATUS CSDriver::SetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
	FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoSetFileSize);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	// おそらくシェルで削除操作が止められている
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
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// ファイルのみ

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
// CreateFile() が必要なもの
//

NTSTATUS CSDriver::Overwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
	BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOverwrite);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	// 合致条件不明
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, FileAttributes=%u, ReplaceFileAttributes=%s, AllocationSize=%llu",
		FileContext->FileName, FileAttributes, BOOL_CSTRW(ReplaceFileAttributes), AllocationSize);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);
	APP_ASSERT(ctx->isFile());

	// ローカルにキャッシュが存在しない場合もあるので、DoDelete() と同じで
	// 他とは異なり、ここでは CREATE_ALWAYS になる
	//
	// --> 上書きなので、既存データを意識する必要がない

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
	APP_ASSERT(!FA_IS_DIRECTORY(FileContext->FileInfo.FileAttributes));		// ファイルのみ

	// 合致条件不明
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, FileAttributes=%u, FileSize=%llu, Offset=%llu, Length=%lu, WriteToEndOfFile=%s, ConstrainedIo=%s",
		FileContext->FileName, FileContext->FileInfo.FileAttributes, FileContext->FileInfo.FileSize,
		Offset, Length, BOOL_CSTRW(WriteToEndOfFile), BOOL_CSTRW(ConstrainedIo));

	return mCSDevice->writeObject(START_CALLER (CSDeviceContext*)FileContext->UParam,
		Buffer, Offset, Length, WriteToEndOfFile, ConstrainedIo, PBytesTransferred, FileInfo);
}

// ---------------------------------------------------------------------------
//
// 以降はファイルとディレクトリの両方が対象
//

NTSTATUS CSDriver::SetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN argDeleteFile)
{
	StatsIncr(DoSetDelete);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	// おそらくシェルで削除操作が止められている
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

	// プロパティで属性を変更した場合
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
		// ローカルにキャッシュが存在している場合のみ属性を変更する
		//
		// --> robocopy /COPY:T 対策

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

	// 本当は STATUS_INVALID_DEVICE_REQUEST としたいが、robocopy が失敗するので
	// 全て無視する

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

	// CMD で "ren old.txt new.txt" を実行すると通過する
	RETURN_ERROR_IF_READONLY();

	traceW(L"FileName=%s, NewFileName=%s, ReplaceIfExists=%s",
		FileName, NewFileName, BOOL_CSTRW(ReplaceIfExists));

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	APP_ASSERT(ctx);

	// 作成対象と同じファイル名が存在するかチェック

	ObjectKey newObjKey;
	auto ntstatus = this->canCreateObject(START_CALLER NewFileName, ctx->isDir(), &newObjKey);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: canCreateObject");

		return ntstatus;
	}

	// TODO: 後で消す ... abort() テスト用
	//APP_ASSERT(newObjKey.key() != L"abort.wcse");

	// rename 処理開始

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