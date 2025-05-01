#pragma once

#include "CSDriverBase.hpp"
#include "ActiveDirInfo.hpp"

extern "C"
{
	CSELIB::ICSDriver* NewCSDriver(PCWSTR argCSDeviceType, PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[], CSELIB::ICSDevice* argCSDevice, WINCSE_DRIVER_STATS* argStats);
}

namespace CSEDRV
{

class CSDriver;

bool makeCacheFilePath(const std::filesystem::path& argDir, const std::wstring& argName, std::filesystem::path* pPath);
NTSTATUS updateFileInfo(HANDLE hFile, FSP_FSCTL_FILE_INFO* pFileInfo);
NTSTATUS syncAttributes(const std::filesystem::path& cacheFilePath, const CSELIB::DirInfoPtr& remoteDirInfo);
NTSTATUS syncContent(CSDriver* driver, FileContext* ctx, CSELIB::FILEIO_OFFSET_T argReadOffset, CSELIB::FILEIO_LENGTH_T argReadLength);


class CSDriver final : public CSDriverBase
{
private:
	ActiveDirInfo mActiveDirInfo;

private:
	using CSDriverBase::CSDriverBase;
	CSELIB::DirInfoPtr getDirInfoByWinPath(CALLER_ARG const std::filesystem::path& argWinPath);

protected:
	// CSDriverBase ÇåoóRÇµÇƒåƒÇ—èoÇ≥ÇÍÇÈä÷êî

	NTSTATUS GetSecurityByName(const std::filesystem::path& argWinPath, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize) override;
	NTSTATUS Open(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS Create(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, FileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) override;
	VOID	 Close(FileContext* ctx) override;
	VOID	 Cleanup(FileContext* ctx, PWSTR argFileName, ULONG argFlags) override;
	NTSTATUS Flush(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS GetFileInfo(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS GetSecurity(FileContext* ctx, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T pSecurityDescriptorSize) override;
	NTSTATUS Overwrite(FileContext* argFileContext, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo) override;
	NTSTATUS Read(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred) override;
	NTSTATUS ReadDirectory(FileContext* ctx, PWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred) override;
	NTSTATUS Rename(FileContext* argFileContext, PWSTR argFileName, PWSTR argNewFileName, BOOLEAN argReplaceIfExists) override;
	NTSTATUS SetBasicInfo(FileContext* argFileContext, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* argFileInfo) override;
	NTSTATUS SetFileSize(FileContext* ctx, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS SetSecurity(FileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor) override;
	NTSTATUS Write(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* pFileInfo) override;
	NTSTATUS SetDelete(FileContext* ctx, PWSTR argFileName, BOOLEAN argDeleteFile) override;

private:
	friend CSELIB::ICSDriver* ::NewCSDriver(PCWSTR argCSDeviceType, PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[], CSELIB::ICSDevice* argCSDevice, WINCSE_DRIVER_STATS* argStats);
	friend NTSTATUS syncContent(CSDriver* driver, FileContext* ctx, CSELIB::FILEIO_OFFSET_T argReadOffset, CSELIB::FILEIO_LENGTH_T argReadLength);
};

}	// namespace CSELIB

// EOF
