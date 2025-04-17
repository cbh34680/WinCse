#pragma once

#include "WinCseLib.h"
#include "CSDeviceBase.hpp"
#include "OpenContext.hpp"
#include "FilePart.hpp"

#define XCOPY_V			(0)
#define XCOPY_DIR		(0)

class CSDevice : public CSDeviceBase
{
private:
	// バケット操作関連

	bool reloadBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold);

	// Read 関連

	NTSTATUS prepareLocalFile_simple(CALLER_ARG OpenContext* ctx, UINT64 argOffset, ULONG argLength);
	bool downloadMultipart(CALLER_ARG OpenContext* ctx, const std::wstring& localPath);

	// Upload
	bool uploadWhenClosing(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx, PCWSTR sourcePath);

	bool putObject(CALLER_ARG const WCSE::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath);
#if XCOPY_V
	bool putObjectViaListLock(CALLER_ARG const WCSE::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath);
#endif

	// ファイル/ディレクトリに特化

	WCSE::DirInfoType makeDirInfoDir1(const std::wstring& argFileName) const
	{
		return WCSE::makeDirInfo(argFileName, mRuntimeEnv->DefaultCommonPrefixTime, FILE_ATTRIBUTE_DIRECTORY | mRuntimeEnv->DefaultFileAttributes);
	}

public:
	void onTimer(CALLER_ARG0) override;
	void onIdle(CALLER_ARG0) override;
	void onNotif(CALLER_ARG DWORD argEventId, PCWSTR argEventName) override;

public:
	explicit CSDevice(const std::wstring& argTempDir, const std::wstring& argIniSection,
		const std::unordered_map<std::wstring, WCSE::IWorker*>& argWorkers)
		:
		CSDeviceBase(argTempDir, argIniSection, argWorkers)
	{
	}

	~CSDevice();

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;

	bool headBucket(CALLER_ARG const std::wstring& argBucket, WCSE::DirInfoType* pDirInfo) override;
	bool listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList) override;
	bool headObject(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoType* pDirInfo) override;
	bool listObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoListType* pDirInfoList) override;
	bool listDisplayObjects(CALLER_ARG const WCSE::ObjectKey& argObjKey, WCSE::DirInfoListType* pDirInfoList) override;
	NTSTATUS renameObject(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx, const WCSE::ObjectKey& argNewObjKey) override;
	bool deleteObject(CALLER_ARG const WCSE::ObjectKey& argObjKey) override;
	WCSE::CSDeviceContext* create(CALLER_ARG const WCSE::ObjectKey& argObjKey, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes) override;
	WCSE::CSDeviceContext* open(CALLER_ARG const WCSE::ObjectKey& argObjKey, UINT32 CreateOptions, UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo) override;
	void close(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx) override;
	NTSTATUS readObject(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) override;
	NTSTATUS writeObject(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) override;
	NTSTATUS setDelete(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx, BOOLEAN argDeleteFile) override;

	NTSTATUS getHandleFromContext(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx,
		DWORD argDesiredAccess, DWORD argCreationDisposition, PHANDLE pHandle) override;
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