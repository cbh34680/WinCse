#pragma once

#include <regex>
#include <set>

class CSDriver : public WCSE::ICSDriver
{
private:
	const std::wstring mIniSection;

	WINCSE_DRIVER_STATS* const mStats;
	WCSE::ICSDevice* const mCSDevice;
	bool mReadonlyVolume = false;

	struct
	{
		std::mutex mGuard;
		std::unordered_map<std::wstring, FSP_FSCTL_FILE_INFO> mFileInfos;
	}
	CreateNew;

	// Worker
	std::unordered_map<std::wstring, WCSE::IWorker*> mWorkers;

	WCSE::IWorker* getWorker(const std::wstring& argName) const noexcept
	{
		APP_ASSERT(mWorkers.find(argName) != mWorkers.cend());

		return mWorkers.at(argName);
	}

	// 無視するファイル名の正規表現
	std::wregex mIgnoreFileNamePatterns;

	// 作業用ディレクトリ (プログラム引数 "-u" から算出される)
	//std::wstring mWorkDir;

	// 属性参照用ファイル・ハンドル
	WCSE::FileHandle mRefFile;
	WCSE::FileHandle mRefDir;

	struct ResourceSweeper
	{
		CSDriver* const mThat;
		std::set<PTFS_FILE_CONTEXT*> mOpenAddrs;

		explicit ResourceSweeper(CSDriver* argThat) noexcept
			:
			mThat(argThat)
		{
		}

		void add(PTFS_FILE_CONTEXT* FileContext) noexcept;
		void remove(PTFS_FILE_CONTEXT* FileContext) noexcept;

		~ResourceSweeper();
	}
	mResourceSweeper;

	enum class FileNameType
	{
		RootDirectory,
		DirectoryObject,
		FileObject,
		Bucket,
	};

	NTSTATUS getFileInfoByFileName(CALLER_ARG PCWSTR fileName,
		FSP_FSCTL_FILE_INFO* pFileInfo, FileNameType* pFileNameType);

	NTSTATUS verifyFileUniqueness(CALLER_ARG
		PCWSTR argFileName, bool argIsDir, WCSE::ObjectKey* pObjKey) const noexcept;

protected:
	bool shouldIgnoreFileName(const std::wstring& FileName) const noexcept;

public:
	explicit CSDriver(
		WINCSE_DRIVER_STATS* argStats, const std::wstring& argTempDir,
		const std::wstring& argIniSection, WCSE::NamedWorker argWorkers[],
		WCSE::ICSDevice* argCSDevice) noexcept;

	~CSDriver();

	// WinFsp から呼び出される関数
	NTSTATUS PreCreateFilesystem(FSP_SERVICE* Service,
		PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	NTSTATUS DoGetSecurityByName(PCWSTR FileName, PUINT32 PFileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize) override;

	NTSTATUS DoOpen(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	VOID DoClose(PTFS_FILE_CONTEXT* FileContext) override;

	VOID DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags) override;

	NTSTATUS DoCreate(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoFlush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
		PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize) override;

	NTSTATUS DoOverwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
		BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoRead(PTFS_FILE_CONTEXT* FileContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	NTSTATUS DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker,
		PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred) override;

	NTSTATUS DoRename(PTFS_FILE_CONTEXT* FileContext,
		PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) override;

	NTSTATUS DoSetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes,
		UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime,
		FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoSetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
		FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoSetSecurity(PTFS_FILE_CONTEXT* FileContext,
		SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor) override;

	NTSTATUS DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
		BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile) override;
};

#define StatsIncr(name)			::InterlockedIncrement(& (this->mStats->name))

// EOF
