#pragma once

#include "WinCseLib.h"
#include "RuntimeEnv.hpp"
#include "ExecuteApi.hpp"
#include "QueryBucket.hpp"
#include "QueryObject.hpp"

class CSDeviceBase : public WCSE::ICSDevice
{
private:
	const std::wstring mIniSection;
	const std::unordered_map<std::wstring, WCSE::IWorker*> mWorkers;

	UINT32 mDefaultFileAttributes = 0U;
	//FSP_SERVICE* mWinFspService = nullptr;

protected:
	//FSP_FILE_SYSTEM* mFileSystem = nullptr;

	// 属性参照用ファイル・ハンドル
	WCSE::FileHandle mRefFile;
	WCSE::FileHandle mRefDir;

	std::unique_ptr<RuntimeEnv> mRuntimeEnv;
	std::unique_ptr<ExecuteApi> mExecuteApi;
	std::unique_ptr<QueryBucket> mQueryBucket;
	std::unique_ptr<QueryObject> mQueryObject;

private:
	bool createNotifListener(CALLER_ARG0);
	void notifListener() noexcept;
	void deleteNotifListener(CALLER_ARG0);

public:
	virtual void onTimer(CALLER_ARG0) = 0;
	virtual void onIdle(CALLER_ARG0) = 0;

protected:
	virtual void onNotif(CALLER_ARG [[maybe_unused]] DWORD argEventId, [[maybe_unused]] PCWSTR argEventName) = 0;

	WCSE::IWorker* getWorker(const std::wstring& argName) const
	{
		return mWorkers.at(argName);
	}

public:
	explicit CSDeviceBase(const std::wstring& argTempDir, const std::wstring& argIniSection,
		const std::unordered_map<std::wstring, WCSE::IWorker*>& argWorkers);

	virtual ~CSDeviceBase() noexcept = default;

	NTSTATUS PreCreateFilesystem(FSP_SERVICE* Service,
		PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) override;

	NTSTATUS OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem) override;
	VOID OnSvcStop() override;
};

// EOF