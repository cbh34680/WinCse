#pragma once

#include <regex>
#include <set>

class WinCse : public WinCseLib::ICSDriver
{
private:
	WINCSE_DRIVER_STATS& mStats;

	WinCseLib::IWorker* mDelayedWorker;
	WinCseLib::IWorker* mIdleWorker;

	WinCseLib::ICSDevice* mCSDevice;

	const std::wstring mTempDir;
	const std::wstring mIniSection;
	int mMaxFileSize;

	// 無視するファイル名の正規表現
	std::wregex mIgnoredFileNamePatterns;

	// 作業用ディレクトリ (プログラム引数 "-u" から算出される)
	std::wstring mWorkDir;

	// 属性参照用ファイル・ハンドル
	HANDLE mFileRefHandle = INVALID_HANDLE_VALUE;
	HANDLE mDirRefHandle = INVALID_HANDLE_VALUE;

	NTSTATUS FileNameToFileInfo(CALLER_ARG const wchar_t* FileName, FSP_FSCTL_FILE_INFO* pFileInfo);
	NTSTATUS HandleToInfo(CALLER_ARG HANDLE handle, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize);

	struct ResourceRAII
	{
		WinCse* mThat;

		ResourceRAII(WinCse* argThat) : mThat(argThat) { }

		std::set<PTFS_FILE_CONTEXT*> mOpenAddrs;
		void add(PTFS_FILE_CONTEXT* FileContext);
		void del(PTFS_FILE_CONTEXT* FileContext);

		~ResourceRAII();
	}
	mResourceRAII;

protected:
	bool isFileNameIgnored(const wchar_t* FileName);

public:
	WinCse(WINCSE_DRIVER_STATS* argStats, const std::wstring& argTempDir, const std::wstring& argIniSection,
		WinCseLib::IWorker* argDelayedWorker, WinCseLib::IWorker* argIdleWorker,
		WinCseLib::ICSDevice* argCSDevice);

	~WinCse();

	// WinFsp から呼び出される関数
	bool PreCreateFilesystem(const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;
	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	NTSTATUS DoGetSecurityByName(const wchar_t* FileName, PUINT32 PFileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) override;

	NTSTATUS DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoClose(PTFS_FILE_CONTEXT* FileContext) override;

	VOID DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags) override;

	NTSTATUS DoCreate() override;
	NTSTATUS DoFlush() override;

	NTSTATUS DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) override;

	NTSTATUS DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo) override;
	NTSTATUS DoOverwrite() override;
	NTSTATUS DoRead(PTFS_FILE_CONTEXT* FileContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	NTSTATUS DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker,
		PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) override;

	NTSTATUS DoRename() override;
	NTSTATUS DoSetBasicInfo() override;
	NTSTATUS DoSetFileSize() override;
	NTSTATUS DoSetPath() override;
	NTSTATUS DoSetSecurity() override;
	NTSTATUS DoWrite() override;

	NTSTATUS DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile) override;
};

#define StatsIncr(name)			::InterlockedIncrement(& (this->mStats.name))

// EOF
