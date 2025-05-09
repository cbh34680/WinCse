#pragma once

#include "CSDriverCommon.h"
#include "RuntimeEnv.hpp"
#include "Protect.hpp"
#include "FileContext.hpp"
#include "FileContextSweeper.hpp"
#include "NotifListener.hpp"

namespace CSEDRV
{

class CSDriverBase : public CSELIB::ICSDriver
{
protected:
	const std::wstring								mDeviceType;
	const std::wstring								mIniSection;
	const std::map<std::wstring, CSELIB::IWorker*>	mWorkers;
	const std::list<ICSService*>					mServices;
	WINCSE_DRIVER_STATS* const						mStats;
	CSELIB::ICSDevice* const						mDevice;
	std::unique_ptr<RuntimeEnv>						mRuntimeEnv;

protected:
	struct FileNameGuard : public CSELIB::SharedBase { };
	CSELIB::ShareStore<FileNameGuard>				mFileNameGuard;

private:
	std::unique_ptr<NotifListener>					mNotifListener;
	FileContextSweeper								mFileContextSweeper;

protected:
	explicit CSDriverBase(const std::wstring& argCSDeviceType, const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers, CSELIB::ICSDevice* argCSDevice, WINCSE_DRIVER_STATS* argStats);

	CSELIB::IWorker* getWorker(const std::wstring& argName) const
	{
		APP_ASSERT(mWorkers.find(argName) != mWorkers.cend());

		return mWorkers.at(argName);
	}

	void applyDefaultFileAttributes(FSP_FSCTL_FILE_INFO* pFileInfo) const;

public:
	void onIdle();

	std::list<std::wstring> getNotificationList() override
	{
		return { L"Global\\WinCse-util-print-report" };
	}

	bool onNotif(const std::wstring& argNotifName) override;

	// WinFsp ‚©‚çŒÄ‚Ño‚³‚ê‚éŠÖ”

	NTSTATUS RelayPreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* argVolumeParams) override;
	NTSTATUS RelayOnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID     RelayOnSvcStop() override;

	NTSTATUS RelayGetSecurityByName(PCWSTR argFileName, PUINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize) override final;
	NTSTATUS RelayOpen(PCWSTR argFileName, UINT32 argCreateOptions, UINT32 argGrantedAccess, CSELIB::IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	NTSTATUS RelayCreate(PCWSTR argFileName, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, CSELIB::IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	VOID     RelayClose(CSELIB::IFileContext* argFileContext) override final;
	VOID     RelayCleanup(CSELIB::IFileContext* argFileContext, PWSTR argFileName, ULONG argFlags) override final;
	NTSTATUS RelayFlush(CSELIB::IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	NTSTATUS RelayGetFileInfo(CSELIB::IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	NTSTATUS RelayGetSecurity(CSELIB::IFileContext* argFileContext, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize) override final;
	NTSTATUS RelayOverwrite(CSELIB::IFileContext* argFileContext, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	NTSTATUS RelayRead(CSELIB::IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred) override final;
	NTSTATUS RelayReadDirectory(CSELIB::IFileContext* argFileContext, PWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred) override final;
	NTSTATUS RelayRename(CSELIB::IFileContext* argFileContext, PWSTR argFileName, PWSTR argNewFileName, BOOLEAN argReplaceIfExists) override final;
	NTSTATUS RelaySetBasicInfo(CSELIB::IFileContext* argFileContext, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	NTSTATUS RelaySetFileSize(CSELIB::IFileContext* argFileContext, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	NTSTATUS RelaySetSecurity(CSELIB::IFileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor) override final;
	NTSTATUS RelayWrite(CSELIB::IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* argFileInfo) override final;
	NTSTATUS RelaySetDelete(CSELIB::IFileContext* argFileContext, PWSTR argFileName, BOOLEAN argDeleteFile) override final;

protected:
	NTSTATUS PreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* argVolumeParams) override;
	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID     OnSvcStop() override;

	virtual NTSTATUS GetSecurityByName(const std::filesystem::path& argWinPath, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize) = 0;
	virtual NTSTATUS Open(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual NTSTATUS Create(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual VOID	 Close(FileContext* ctx) = 0;
	virtual VOID	 Cleanup(FileContext* ctx, PCWSTR argWinPath, ULONG argFlags) = 0;
	virtual NTSTATUS Flush(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual NTSTATUS GetFileInfo(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual NTSTATUS GetSecurity(FileContext* ctx, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T pSecurityDescriptorSize) = 0;
	virtual NTSTATUS Overwrite(FileContext* ctx, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual NTSTATUS Read(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred) = 0;
	virtual NTSTATUS ReadDirectory(FileContext* ctx, PCWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred) = 0;
	virtual NTSTATUS Rename(FileContext* ctx, const std::filesystem::path& argSrcWinPath, const std::filesystem::path& argDstWinPath, BOOLEAN argReplaceIfExists) = 0;
	virtual NTSTATUS SetBasicInfo(FileContext* ctx, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual NTSTATUS SetFileSize(FileContext* ctx, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual NTSTATUS SetSecurity(FileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor) = 0;
	virtual NTSTATUS Write(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;
	virtual NTSTATUS SetDelete(FileContext* ctx, PCWSTR argFileName, BOOLEAN argDeleteFile) = 0;
};

}	// namespace CSELIB

#define StatsIncr(name)			::InterlockedIncrement(& (this->mStats->name))

// EOF