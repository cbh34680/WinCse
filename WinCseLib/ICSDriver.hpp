#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

typedef struct
{
	long RelayPreCreateFilesystem;
	long RelayOnSvcStart;
	long RelayOnSvcStop;

	long RelayGetSecurityByName;
	long RelayOpen;
	long RelayClose;
	long RelayGetFileInfo;
	long RelayGetSecurity;
	long RelayOverwrite;
	long RelayRead;
	long RelayReadDirectory;
	long RelayCleanup;
	long RelayCreate;
	long RelayFlush;
	long RelayRename;
	long RelaySetBasicInfo;
	long RelaySetFileSize;
	long RelaySetSecurity;
	long RelayWrite;
	long RelaySetDelete;
}
WINCSE_DRIVER_STATS;

namespace CSELIB {

struct ICSDriver : public ICSService
{
	virtual NTSTATUS RelayPreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* argVolumeParams) { return STATUS_SUCCESS; }
	virtual NTSTATUS RelayOnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) { return STATUS_SUCCESS; }
	virtual VOID     RelayOnSvcStop() { }

	virtual NTSTATUS RelayGetSecurityByName(PCWSTR argFileName, PUINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayCreate(PCWSTR argFileName, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayOpen(PCWSTR argFileName, UINT32 argCreateOptions, UINT32 argGrantedAccess, IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayOverwrite(IFileContext* argFileContext, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual VOID     RelayCleanup(IFileContext* argFileContext, PWSTR argFileName, ULONG argFlags) { }
	virtual VOID     RelayClose(IFileContext* argFileContext) { }
	virtual NTSTATUS RelayRead(IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayWrite(IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayFlush(IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayGetFileInfo(IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelaySetBasicInfo(IFileContext* argFileContext, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelaySetFileSize(IFileContext* argFileContext, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayRename(IFileContext* argFileContext, PWSTR argFileName,PWSTR argNewFileName, BOOLEAN argReplaceIfExists) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayGetSecurity(IFileContext* argFileContext, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelaySetSecurity(IFileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelayReadDirectory(IFileContext* argFileContext, PWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred) { return STATUS_INVALID_DEVICE_REQUEST; }
	virtual NTSTATUS RelaySetDelete(IFileContext* argFileContext, PWSTR argFileName, BOOLEAN argDeleteFile) { return STATUS_INVALID_DEVICE_REQUEST; }
};

} // namespace CSELIB

#pragma warning(pop)
// EOF