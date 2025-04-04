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
	long DoSetSecurity;
	long DoWrite;
	long DoSetDelete;

	long _CallCreate;
	long _CallOpen;
	long _CallClose;
	long _ForceClose;
}
WINCSE_DRIVER_STATS;

namespace WCSE {

struct ICSDriver : public ICSService
{
	virtual ~ICSDriver() = default;

	// READ
	virtual NTSTATUS DoGetSecurityByName(const wchar_t* FileName, PUINT32 PFileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoClose(PTFS_FILE_CONTEXT* FileContext) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoGetSecurity(PTFS_FILE_CONTEXT* FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual void DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo) { }

	virtual NTSTATUS DoOverwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
		BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoRead(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
		PULONG PBytesTransferred) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoReadDirectory(
		PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker,
		PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual VOID DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags) { }

	virtual NTSTATUS DoCreate(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoFlush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO *FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoRename(PTFS_FILE_CONTEXT* FileContext,
		PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoSetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
		const UINT64 CreationTime, const UINT64 LastAccessTime, const UINT64 LastWriteTime,
		const UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoSetFileSize(PTFS_FILE_CONTEXT* FileContext,
		UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoSetSecurity(PTFS_FILE_CONTEXT* FileContext,
		SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
		BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }

	virtual NTSTATUS DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile) { return STATUS_INVALID_DEVICE_REQUEST; }
};

} // namespace WCSE

#pragma warning(pop)
// EOF