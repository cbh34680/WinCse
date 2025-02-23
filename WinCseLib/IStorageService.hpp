#pragma once

namespace WinCseLib {

struct WINCSELIB_API IStorageService : public IService
{
	virtual ~IStorageService() = default;

	// READ
	virtual NTSTATUS DoGetSecurityByName(
		const wchar_t* FileName, PUINT32 PFileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) = 0;

	virtual NTSTATUS DoOpen(
		const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) = 0;

	virtual NTSTATUS DoClose(PTFS_FILE_CONTEXT* FileContext) = 0;
	virtual NTSTATUS DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS DoGetSecurity(PTFS_FILE_CONTEXT* FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) = 0;
	virtual NTSTATUS DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo) = 0;
	virtual NTSTATUS DoOverwrite() = 0;
	virtual NTSTATUS DoRead(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;

	virtual NTSTATUS DoReadDirectory(
		PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker,
		PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) = 0;

	// WRITE
	virtual NTSTATUS DoCanDelete() = 0;
	virtual NTSTATUS DoCleanup() = 0;
	virtual NTSTATUS DoCreate() = 0;
	virtual NTSTATUS DoFlush() = 0;
	virtual NTSTATUS DoRename() = 0;
	virtual NTSTATUS DoSetBasicInfo() = 0;
	virtual NTSTATUS DoSetFileSize() = 0;
	virtual NTSTATUS DoSetPath() = 0;
	virtual NTSTATUS DoSetSecurity() = 0;
	virtual NTSTATUS DoWrite() = 0;
	virtual NTSTATUS DoSetDelete() = 0;
};

} // namespace WinCseLib

// EOF