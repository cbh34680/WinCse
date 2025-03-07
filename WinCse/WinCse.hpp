#pragma once

#include <regex>

// ��������o�P�b�g���ƃL�[�ɕ���
class BucketKey
{
private:
	std::wstring mBucket;
	std::wstring mKey;

	bool mHasKey = false;
	bool mOK = false;

public:
	const std::wstring& bucket() const { return mBucket; }
	const std::wstring& key() const { return mKey; }
	bool hasKey() const { return mHasKey; }
	bool OK() const { return mOK; }

	BucketKey(const wchar_t* wstr);
};

class WinCse : public WinCseLib::ICSDriver
{
private:
	WinCseLib::IWorker* mDelayedWorker;
	WinCseLib::IWorker* mIdleWorker;

	WinCseLib::ICSDevice* mCSDevice;

	const std::wstring mTempDir;
	const std::wstring mIniSection;
	int mMaxFileSize;

	// ��������t�@�C�����̐��K�\��
	std::wregex mIgnoredFileNamePatterns;

	// ��Ɨp�f�B���N�g�� (�v���O�������� "-u" ����Z�o�����)
	std::wstring mWorkDir;

	// �����Q�Ɨp�t�@�C���E�n���h��
	HANDLE mFileRefHandle = INVALID_HANDLE_VALUE;
	HANDLE mDirRefHandle = INVALID_HANDLE_VALUE;

	NTSTATUS FileNameToFileInfo(CALLER_ARG const wchar_t* FileName, FSP_FSCTL_FILE_INFO* pFileInfo);
	NTSTATUS HandleToInfo(CALLER_ARG HANDLE handle, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize);

protected:
	bool isFileNameIgnored(const wchar_t* FileName);

public:
	WinCse(const std::wstring& argTempDir, const std::wstring& argIniSection,
		WinCseLib::IWorker* argDelayedWorker, WinCseLib::IWorker* argIdleWorker,
		WinCseLib::ICSDevice* argCSDevice);

	~WinCse();

	// WinFsp ����Ăяo�����֐�
	bool PreCreateFilesystem(const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;
	bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	void OnSvcStop() override;

	NTSTATUS DoGetSecurityByName(const wchar_t* FileName, PUINT32 PFileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) override;

	NTSTATUS DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
		PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS DoClose(PTFS_FILE_CONTEXT* FileContext) override;

	NTSTATUS DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags) override;

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

public:
	WINCSE_DRIVER_STATS mStats = {};
};

#define StatsIncr(name)			::InterlockedIncrement(& (this->mStats.name))

// EOF
