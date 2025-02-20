#pragma once

#include <string>
#include <regex>


class WinCse : public IStorageService
{
private:
	const std::wstring mTempDir;
	const wchar_t* mIniSection;
	IWorker* mDelayedWorker;
	IWorker* mIdleWorker;
	ICloudStorage* mStorage;
	int mMaxFileSize;

	// 無視するファイル名の正規表現
	const std::wregex mIgnoreFileNamePattern;

	// 作業用ディレクトリ (プログラム引数 "-u" から算出される)
	std::wstring mWorkDir;

	// 属性参照用ファイル・ハンドル
	HANDLE mFileRefHandle = INVALID_HANDLE_VALUE;
	HANDLE mDirRefHandle = INVALID_HANDLE_VALUE;

	NTSTATUS FileNameToFileInfo(const wchar_t* FileName, FSP_FSCTL_FILE_INFO* pFileInfo);
	NTSTATUS HandleToInfo(HANDLE handle, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize);

protected:
	bool isIgnoreFileName(const wchar_t* FileName);

public:
	WinCse(const wchar_t* tmpdir, const wchar_t* iniSection,
		IWorker* delayedWorker, IWorker* idleWorker,
		ICloudStorage* storage);

	~WinCse();

	// WinFsp から呼び出される関数
	bool OnSvcStart(const wchar_t* argWorkDir) override;
	void OnSvcStop() override;

	void UpdateVolumeParams(FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;

	NTSTATUS DoGetSecurityByName(const wchar_t* FileName, PUINT32 PFileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) override;

	NTSTATUS DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoClose(PTFS_FILE_CONTEXT* FileContext) override;

	NTSTATUS DoCanDelete() override;
	NTSTATUS DoCleanup() override;
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
	NTSTATUS DoSetDelete() override;
};

// EOF
