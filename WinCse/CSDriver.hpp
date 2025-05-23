#pragma once

#include "CSDriverBase.hpp"
#include "OpenDirEntry.hpp"

CSELIB::ICSDriver* NewCSDriver(PCWSTR argCSDeviceType, PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[], CSELIB::ICSDevice* argCSDevice, WINCSE_DRIVER_STATS* argStats);

namespace CSEDRV
{

bool resolveCacheFilePath(const std::filesystem::path& argDir, const std::wstring& argWinPath, std::filesystem::path* pPath);
NTSTATUS syncAttributes(const CSELIB::DirEntryType& remoteDirEntry, const std::filesystem::path& cacheFilePath);


class CSDriver final : public CSDriverBase
{
private:
	OpenDirEntry mOpenDirEntry;

private:
	using CSDriverBase::CSDriverBase;

	CSELIB::DirEntryType getDirEntryByWinPath(CALLER_ARG const std::filesystem::path& argWinPath) const;
	NTSTATUS canCreateObject(CALLER_ARG const std::filesystem::path& argWinPath, bool argIsDir, std::optional<CSELIB::ObjectKey>* pOptObjKey);
	NTSTATUS syncContent(FileContext* ctx, CSELIB::FILEIO_OFFSET_T argReadOffset, CSELIB::FILEIO_LENGTH_T argReadLength);
	NTSTATUS updateFileInfo(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo, bool argRemoteSizeAware);

protected:
	// CSDriverBase ÇåoóRÇµÇƒåƒÇ—èoÇ≥ÇÍÇÈä÷êî

	NTSTATUS GetSecurityByName(const std::filesystem::path& argWinPath, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize) override;
	NTSTATUS Open(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS Create(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	VOID	 Close(FileContext* ctx) override;
	VOID	 Cleanup(FileContext* ctx, PCWSTR argWinPath, ULONG argFlags) override;
	NTSTATUS Flush(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS GetFileInfo(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS GetSecurity(FileContext* ctx, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T pSecurityDescriptorSize) override;
	NTSTATUS Overwrite(FileContext* ctx, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS Read(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred) override;
	NTSTATUS ReadDirectory(FileContext* ctx, PCWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred) override;
	NTSTATUS Rename(FileContext* ctx, const std::filesystem::path& argSrcWinPath, const std::filesystem::path& argDstWinPath, BOOLEAN argReplaceIfExists) override;
	NTSTATUS SetBasicInfo(FileContext* ctx, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS SetFileSize(FileContext* ctx, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS SetSecurity(FileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor) override;
	NTSTATUS Write(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS SetDelete(FileContext* ctx, PCWSTR argFileName, BOOLEAN argDeleteFile) override;

private:
	friend CSELIB::ICSDriver* ::NewCSDriver(PCWSTR argCSDeviceType, PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[], CSELIB::ICSDevice* argCSDevice, WINCSE_DRIVER_STATS* argStats);
};

}	// namespace CSELIB

// EOF
