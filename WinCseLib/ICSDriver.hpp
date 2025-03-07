#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

typedef struct
{
	long PreCreateFilesystem;
	long OnSvcStart;
	long OnSvcStop;
	long DoGetSecurityByName;
	long DoOpen;
	long DoClose;
	long DoGetFileInfo;
	long DoGetSecurity;
	long DoGetVolumeInfo;
	long DoOverwrite;
	long DoRead;
	long DoReadDirectory;
	long DoCleanup;
	long DoCreate;
	long DoFlush;
	long DoRename;
	long DoSetBasicInfo;
	long DoSetFileSize;
	long DoSetPath;
	long DoSetSecurity;
	long DoWrite;
	long DoSetDelete;

	long _CallCloseFile;
}
WINCSE_DRIVER_STATS;

namespace WinCseLib {

struct WINCSELIB_API ICSDriver : public ICSService
{
	virtual ~ICSDriver() = default;

	// READ
	virtual NTSTATUS DoGetSecurityByName(
		const wchar_t* FileName, PUINT32 PFileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoOpen(
		const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoClose(PTFS_FILE_CONTEXT* FileContext) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoGetSecurity(PTFS_FILE_CONTEXT* FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoOverwrite() { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoRead(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoReadDirectory(
		PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker,
		PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) { return STATUS_INVALID_DEVICE_REQUEST; }

	// WRITE
	virtual NTSTATUS DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoCreate() { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS DoFlush() { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS DoRename() { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS DoSetBasicInfo() { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS DoSetFileSize() { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS DoSetPath() { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS DoSetSecurity() { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS DoWrite() { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile) { return STATUS_INVALID_DEVICE_REQUEST; }
};

} // namespace WinCseLib

#pragma warning(pop)
// EOF