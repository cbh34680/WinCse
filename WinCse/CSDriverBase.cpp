#include "CSDriverBase.hpp"
#include <iostream>

using namespace CSELIB;
using namespace CSEDRV;


static PCWSTR CACHE_DATA_DIR_FNAME		= L"cache\\data";
static PCWSTR CACHE_REPORT_DIR_FNAME	= L"cache\\report";


static std::list<ICSService*> toServices(CSDriverBase* argThat, ICSDevice* argCSDevice, const std::map<std::wstring, IWorker*>& argWorkers)
{
	std::list<ICSService*> services{ argThat, argCSDevice };

	std::transform(argWorkers.cbegin(), argWorkers.cend(), std::back_inserter(services),
		[](const auto& pair) { return pair.second; });

	return services;
}

CSDriverBase::CSDriverBase(
	const std::wstring& argCSDeviceType,
	const std::wstring& argIniSection,
	const std::map<std::wstring, IWorker*>& argWorkers,
	ICSDevice* argCSDevice,
	WINCSE_DRIVER_STATS* argStats)
	:
	mDeviceType(argCSDeviceType),
	mIniSection(argIniSection),
	mWorkers(argWorkers),
	mDevice(argCSDevice),
	mStats(argStats),
	mServices(toServices(this, argCSDevice, argWorkers))
{
	APP_ASSERT(argCSDevice);
}

//
// プログラム引数 "-u" から算出されたディレクトリから ini ファイルを読み
// S3 クライアントを生成する
//
NTSTATUS CSDriverBase::PreCreateFilesystem(FSP_SERVICE*, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* argVolumeParams)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(argVolumeParams);

	traceW(L"argWorkDir=%s", argWorkDir);

	APP_ASSERT(std::filesystem::exists(argWorkDir));
	APP_ASSERT(std::filesystem::is_directory(argWorkDir));

	// argVolumeParams の設定

	argVolumeParams->CaseSensitiveSearch = 1;

	const UINT32 Timeout = 3000U;

	argVolumeParams->FileInfoTimeout = Timeout;

	//argVolumeParams->VolumeInfoTimeout = Timeout;
	argVolumeParams->DirInfoTimeout = Timeout;
	//argVolumeParams->SecurityTimeout = Timeout;
	//argVolumeParams->StreamInfoTimeout = Timeout;
	//argVolumeParams->EaTimeout = Timeout;

	//argVolumeParams->VolumeInfoTimeoutValid = 1;
	argVolumeParams->DirInfoTimeoutValid = 1;
	//argVolumeParams->SecurityTimeoutValid = 1;
	//argVolumeParams->StreamInfoTimeoutValid = 1;
	//argVolumeParams->EaTimeoutValid = 1;

	//
	// ini ファイルから値を取得
	//
	const auto confPath{ std::filesystem::path{ argWorkDir } / CONFIGFILE_FNAME };

	traceW(L"confPath=%s", confPath.c_str());

	// 読み取り専用

	const bool readonly = GetIniBoolW(confPath, mIniSection, L"readonly", false);
	if (readonly)
	{
		// ボリュームの設定

		argVolumeParams->ReadOnlyVolume = 1;
	}

	return STATUS_SUCCESS;
}

struct IdleTask : public IScheduledTask
{
	CSDriverBase* mThat;

	IdleTask(CSDriverBase* argThat)
		:
		mThat(argThat)
	{
	}

	bool shouldRun(int argTick) const override
	{
		// 10 分間隔で run() を実行

		return argTick % 10 == 0;
	}

	void run(int) override
	{
		mThat->onIdle();
	}
};

NTSTATUS CSDriverBase::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(FileSystem);

	traceW(L"argWorkDir=%s", argWorkDir);

	const std::filesystem::path workDir{ argWorkDir };
	const auto confPath{ workDir / CONFIGFILE_FNAME };

	traceW(L"confPath=%s", confPath.c_str());

	//
	// 属性参照用ファイル/ディレクトリの準備
	//
	FileHandle dirSecRef = ::CreateFileW
	(
		argWorkDir,
		FILE_READ_ATTRIBUTES | READ_CONTROL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL
	);

	if (dirSecRef.invalid())
	{
		errorW(L"fault: CreateFileW, argWorkDir=%s", argWorkDir);
		//return STATUS_INSUFFICIENT_RESOURCES;
		return FspNtStatusFromWin32(::GetLastError());
	}

	FileHandle fileSecRef = ::CreateFileW
	(
		confPath.c_str(),
		FILE_READ_ATTRIBUTES | READ_CONTROL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,		// 共有モード
		NULL,														// セキュリティ属性
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL														// テンプレートなし
	);

	if (fileSecRef.invalid())
	{
		errorW(L"fault: CreateFileW, confPath=%s", confPath.c_str());
		//return STATUS_INSUFFICIENT_RESOURCES;
		return FspNtStatusFromWin32(::GetLastError());
	}

	// ファイル・キャッシュ保存用ディレクトリの準備

	const auto cacheDataDir{ workDir / mDeviceType / CACHE_DATA_DIR_FNAME };
	if (!mkdirIfNotExists(cacheDataDir))
	{
		errorW(L"fault: mkdirIfNotExists cacheDataDir=%s", cacheDataDir.c_str());
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	const auto cacheReportDir{ workDir / mDeviceType / CACHE_REPORT_DIR_FNAME };
	if (!mkdirIfNotExists(cacheReportDir))
	{
		errorW(L"fault: mkdirIfNotExists cacheReportDir=%s", cacheReportDir.c_str());
		return STATUS_INSUFFICIENT_RESOURCES;
	}

#ifdef _DEBUG
	forEachFiles(cacheDataDir, [this, &LOG_BLOCK()](const auto& wfd, const auto& fullPath)
	{
		APP_ASSERT(!FA_IS_DIR(wfd.dwFileAttributes));

		traceW(L"cache file: [%s]", fullPath.c_str());
	});
#endif

	// 読み取り専用

	const UINT32 defaultFileAttributes = GetIniBoolW(confPath, mIniSection, L"readonly", false) ? FILE_ATTRIBUTE_READONLY : 0;

	// 実行時変数

	auto runtimeEnv = std::make_unique<RuntimeEnv>(
		//         ini-path     section         key                             default   min         max
		//----------------------------------------------------------------------------------------------------
		cacheDataDir,
		GetIniIntW(confPath,    mIniSection,    L"cache_file_retention_min",        60,		1,	   10080),
		cacheReportDir,
		STCTimeToWinFileTime100nsW(argWorkDir),
		defaultFileAttributes,
		GetIniBoolW(confPath,   mIniSection,    L"delete_after_upload",         false),
		GetIniIntW(confPath,    mIniSection,    L"delete_dir_condition",             2,		1,		   2),
		std::move(dirSecRef),
		std::move(fileSecRef),
		GetIniBoolW(confPath,	mIniSection,	L"readonly",					false),
		GetIniIntW(confPath,	mIniSection,	L"transfer_read_size_mib",			10,		5,		 100)
	);

	traceW(L"runtimeEnv=%s", runtimeEnv->str().c_str());

	mRuntimeEnv = std::move(runtimeEnv);

	APP_ASSERT(dirSecRef.invalid());
	APP_ASSERT(fileSecRef.invalid());

	// 外部からの通知受付を開始

	mNotifListener = NotifListener::create(mServices);
	if (!mNotifListener)
	{
		errorW(L"fault: NotifListener");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	mNotifListener->start();

	// アイドル時のタスクを登録

	getWorker(L"timer")->addTask(new IdleTask{ this });

	return STATUS_SUCCESS;
}

VOID CSDriverBase::OnSvcStop()
{
	mNotifListener->stop();
}

void CSDriverBase::applyDefaultFileAttributes(FSP_FSCTL_FILE_INFO* pFileInfo) const
{
	if (mRuntimeEnv->DefaultFileAttributes)
	{
		// デフォルトのファイル属性を反映する

		if (pFileInfo->FileAttributes & FILE_ATTRIBUTE_NORMAL)
		{
			// FILE_ATTRIBUTE_NORMAL のビットを落とす

			pFileInfo->FileAttributes &= ~FILE_ATTRIBUTE_NORMAL;

			APP_ASSERT(!pFileInfo->FileAttributes);
		}

		pFileInfo->FileAttributes |= mRuntimeEnv->DefaultFileAttributes;
	}
}

// EOF