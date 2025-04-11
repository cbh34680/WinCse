#pragma once

#include "AwsS3C.hpp"
#include "Protect.hpp"

//
// open() が呼ばれたときに UParam として PTFS_FILE_CONTEXT に保存する内部情報
// close() で削除される
//
struct OpenContext : public WCSE::CSDeviceContext
{
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;

	explicit OpenContext(
		const std::wstring& argCacheDataDir,
		const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const UINT32 argCreateOptions,
		const UINT32 argGrantedAccess)
		:
		CSDeviceContext(argCacheDataDir, argObjKey, argFileInfo),
		mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess)
	{
	}

	NTSTATUS openFileHandle(CALLER_ARG DWORD argDesiredAccess, DWORD argCreationDisposition);
};

class AwsS3 : public AwsS3C
{
private:
	// ファイル生成時の排他制御
	struct PrepareLocalFileShare : public SharedBase { };
	ShareStore<PrepareLocalFileShare> mPrepareLocalFileShare;

	// バケット操作関連
	bool reloadListBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold);

	// Read 関連

	NTSTATUS prepareLocalFile_simple(CALLER_ARG OpenContext* ctx, UINT64 argOffset, ULONG argLength);
	bool downloadMultipart(CALLER_ARG OpenContext* ctx, const std::wstring& localPath);

	// Upload
	bool uploadWhenClosing(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext, const std::wstring& localPath);

	bool putObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo,
		const std::wstring& argFilePath);

public:
	void onIdle(CALLER_ARG0);

public:
	using AwsS3C::AwsS3C;

	~AwsS3();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;
	bool listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */) override;

	bool headObject(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) override;

	bool listObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList /* nullable */) override;

	bool listDisplayObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		WCSE::DirInfoListType* pDirInfoList) override;

	bool deleteObject(CALLER_ARG const WCSE::ObjectKey& argObjKey) override;

	bool renameObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		const WCSE::ObjectKey& argNewObjKey) override;

	WCSE::CSDeviceContext* create(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes) override;

	WCSE::CSDeviceContext* open(CALLER_ARG const WCSE::ObjectKey& argObjKey,
		UINT32 CreateOptions, UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo) override;

	void close(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext) override;

	NTSTATUS readObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;

	NTSTATUS writeObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) override;

	NTSTATUS getHandleFromContext(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
		DWORD argDesiredAccess, DWORD argCreationDisposition, PHANDLE pHandle) override;

private:
};

#ifdef WINCSEAWSS3_EXPORTS
#define AWSS3_API __declspec(dllexport)
#else
#define AWSS3_API __declspec(dllimport)
#endif

extern "C"
{
	AWSS3_API WCSE::ICSDevice* NewCSDevice(PCWSTR argTempDir, PCWSTR argIniSection, WCSE::NamedWorker argWorkers[]);
}

// EOF