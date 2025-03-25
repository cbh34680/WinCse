#pragma once

#include <regex>
#include <set>

class WinCse : public WinCseLib::ICSDriver
{
private:
	WINCSE_DRIVER_STATS* mStats;

	bool mReadonlyVolume = false;

	WinCseLib::ICSDevice* mCSDevice;

	const std::wstring mTempDir;
	const std::wstring mIniSection;
	int mMaxFileSize;

	// Worker 取得
	std::unordered_map<std::wstring, WinCseLib::IWorker*> mWorkers;

	WinCseLib::IWorker* getWorker(const std::wstring& argName)
	{
		return mWorkers.at(argName);
	}

	// 無視するファイル名の正規表現
	std::wregex mIgnoredFileNamePatterns;

	// 作業用ディレクトリ (プログラム引数 "-u" から算出される)
	std::wstring mWorkDir;

	// 属性参照用ファイル・ハンドル
	WinCseLib::FileHandle mRefFile;
	WinCseLib::FileHandle mRefDir;

	NTSTATUS FileNameToFileInfo(CALLER_ARG const wchar_t* FileName, FSP_FSCTL_FILE_INFO* pFileInfo);

	NTSTATUS HandleToInfo(CALLER_ARG HANDLE handle, PUINT32 PFileAttributes /* nullable */,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize /* nullable */);

	struct ResourceSweeper
	{
		WinCse* mThat;

		ResourceSweeper(WinCse* argThat) : mThat(argThat) { }

		std::set<PTFS_FILE_CONTEXT*> mOpenAddrs;
		void add(PTFS_FILE_CONTEXT* FileContext);
		void remove(PTFS_FILE_CONTEXT* FileContext);

		~ResourceSweeper();
	}
	mResourceSweeper;

protected:
	bool isFileNameIgnored(const std::wstring& FileName);

public:
	WinCse(
		WINCSE_DRIVER_STATS* argStats, const std::wstring& argTempDir, const std::wstring& argIniSection,
		WinCseLib::NamedWorker argWorkers[], WinCseLib::ICSDevice* argCSDevice);

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

	NTSTATUS DoCreate(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoFlush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO *FileInfo) override;

	NTSTATUS DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) override;

	NTSTATUS DoOverwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
		BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo) override;

	NTSTATUS DoRead(PTFS_FILE_CONTEXT* FileContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	NTSTATUS DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker,
		PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) override;

	NTSTATUS DoRename(PTFS_FILE_CONTEXT* FileContext,
		PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) override;

	NTSTATUS DoSetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
		UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime,
		FSP_FSCTL_FILE_INFO *FileInfo) override;

	NTSTATUS DoSetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
		FSP_FSCTL_FILE_INFO *FileInfo) override;

	NTSTATUS DoSetSecurity(PTFS_FILE_CONTEXT* FileContext,
		SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor) override;

	NTSTATUS DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
		BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) override;

	NTSTATUS DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile) override;
};

#define StatsIncr(name)			::InterlockedIncrement(& (this->mStats->name))

// EOF
