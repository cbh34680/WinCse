#pragma once

#include "WinCseLib.h"
#include "Protect.hpp"

class CSDriverBase : public WCSE::ICSDriver
{
private:
	struct FileNameGuard : public SharedBase { };
	ShareStore<FileNameGuard> mFileNameGuard;

public:
	virtual ~CSDriverBase() = default;

	// WinFsp Ç©ÇÁåƒÇ—èoÇ≥ÇÍÇÈä÷êî

	NTSTATUS DoGetSecurityByName(PCWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize) override final;
	NTSTATUS DoOpen(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	NTSTATUS DoCreate(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	VOID     DoClose(PTFS_FILE_CONTEXT* FileContext) override final;
	VOID     DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags) override final;
	NTSTATUS DoFlush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	NTSTATUS DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	NTSTATUS DoGetSecurity(PTFS_FILE_CONTEXT* FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize) override final;
	NTSTATUS DoOverwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	NTSTATUS DoRead(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override final;
	NTSTATUS DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) override final;
	NTSTATUS DoRename(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) override final;
	NTSTATUS DoSetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	NTSTATUS DoSetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	NTSTATUS DoSetSecurity(PTFS_FILE_CONTEXT* FileContext, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor) override final;
	NTSTATUS DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) override final;
	NTSTATUS DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile) override final;

protected:
	virtual NTSTATUS GetSecurityByName(PCWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize) = 0;
	virtual NTSTATUS Open(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS Create(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual VOID	 Close(PTFS_FILE_CONTEXT* FileContext) = 0;
	virtual VOID	 Cleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags) = 0;
	virtual NTSTATUS Flush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS GetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS GetSecurity(PTFS_FILE_CONTEXT* FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize) = 0;
	virtual NTSTATUS Overwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS Read(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;
	virtual NTSTATUS ReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) = 0;
	virtual NTSTATUS Rename(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) = 0;
	virtual NTSTATUS SetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS SetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS SetSecurity(PTFS_FILE_CONTEXT* FileContext, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor) = 0;
	virtual NTSTATUS Write(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
	virtual NTSTATUS SetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile) = 0;
};

// EOF