#include "WinCseLib.h"
#include "CSDriver.hpp"
#include <filesystem>
#include <iostream>

using namespace WCSE;


static PCWSTR CONFIGFILE_FNAME = L"WinCse.conf";

//
// プログラム引数 "-u" から算出されたディレクトリから ini ファイルを読み
// S3 クライアントを生成する
//
NTSTATUS CSDriver::PreCreateFilesystem(FSP_SERVICE *Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
	StatsIncr(PreCreateFilesystem);

	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(VolumeParams);

	traceW(L"argWorkDir=%s", argWorkDir);

	namespace fs = std::filesystem;

	APP_ASSERT(fs::exists(argWorkDir));
	APP_ASSERT(fs::is_directory(argWorkDir));

	// VolumeParams の設定

	VolumeParams->CaseSensitiveSearch = 1;

	const UINT32 Timeout = 3000U;

	VolumeParams->FileInfoTimeout = Timeout;

	//VolumeParams->VolumeInfoTimeout = Timeout;
	VolumeParams->DirInfoTimeout = Timeout;
	//VolumeParams->SecurityTimeout = Timeout;
	//VolumeParams->StreamInfoTimeout = Timeout;
	//VolumeParams->EaTimeout = Timeout;

	//VolumeParams->VolumeInfoTimeoutValid = 1;
	VolumeParams->DirInfoTimeoutValid = 1;
	//VolumeParams->SecurityTimeoutValid = 1;
	//VolumeParams->StreamInfoTimeoutValid = 1;
	//VolumeParams->EaTimeoutValid = 1;

	//
	// ini ファイルから値を取得
	//
	const auto workDir{ fs::path(argWorkDir).wstring() };
	const auto confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

	traceW(L"confPath=%s", confPath.c_str());

	// 読み取り専用

	const bool readonly = ::GetPrivateProfileIntW(mIniSection.c_str(), L"readonly", 0, confPath.c_str()) != 0;
	if (readonly)
	{
		// ボリュームの設定

		VolumeParams->ReadOnlyVolume = 1;
	}

	// PreCreateFilesystem() の伝播

	for (const auto& it: mWorkers)
	{
		const auto worker = it.second;
		const auto klassName = getDerivedClassNamesW(worker);

		traceW(L"%s::PreCreateFilesystem()", klassName.c_str());

		const auto ntstatus = worker->PreCreateFilesystem(Service, argWorkDir, VolumeParams);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: PreCreateFilesystem");
			return ntstatus;
		}
	}

	ICSService* services[] = { mCSDevice };

	for (int i=0; i<_countof(services); i++)
	{
		const auto service = services[i];
		const auto klassName = getDerivedClassNamesW(service);

		traceW(L"%s::PreCreateFilesystem()", klassName.c_str());

		const auto ntstatus = services[i]->PreCreateFilesystem(Service, argWorkDir, VolumeParams);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: PreCreateFilesystem[%d]", i);
			return ntstatus;
		}
	}

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
	StatsIncr(OnSvcStart);

	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(FileSystem);

	namespace fs = std::filesystem;

	traceW(L"argWorkDir=%s", argWorkDir);

	const auto workDir{ fs::path(argWorkDir).wstring() };
	const auto confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

	traceW(L"confPath=%s", confPath.c_str());


	// 無視するファイル名のパターン

	std::wstring re_ignore_patterns;
	GetIniStringW(confPath.c_str(), mIniSection.c_str(), L"re_ignore_patterns", &re_ignore_patterns);

	if (!re_ignore_patterns.empty())
	{
		try
		{
			// conf で指定された正規表現パターンの整合性テスト
			// 不正なパターンの場合は例外で catch されるので反映されない

			std::wregex reTest{ re_ignore_patterns, std::regex_constants::icase };

			// OK
			mIgnoreFileNamePatterns = std::move(reTest);
		}
		catch (const std::regex_error& ex)
		{
			traceA("regex_error: %s", ex.what());
			traceW(L"%s: ignored, set default patterns", re_ignore_patterns.c_str());
		}
	}

	//
	// 属性参照用ファイル/ディレクトリの準備
	//
	mRefFile = ::CreateFileW
	(
		confPath.c_str(),
		FILE_READ_ATTRIBUTES | READ_CONTROL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,		// 共有モード
		NULL,														// セキュリティ属性
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL														// テンプレートなし
	);

	if (mRefFile.invalid())
	{
		traceW(L"fault: CreateFileW, confPath=%s", confPath.c_str());
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	mRefDir = ::CreateFileW
	(
		argWorkDir,
		FILE_READ_ATTRIBUTES | READ_CONTROL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL
	);

	if (mRefDir.invalid())
	{
		traceW(L"fault: CreateFileW, argWorkDir=%s", argWorkDir);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// メンバに保存

	mWorkDir = workDir;

	traceW(L"INFO: TempDir=%s, WorkDir=%s", mTempDir.c_str(), mWorkDir.c_str());

	// OnSvcStart() の伝播

	for (const auto& it: mWorkers)
	{
		const auto worker = it.second;
		const auto klassName = getDerivedClassNamesW(worker);

		traceW(L"%s::OnSvcStart()", klassName.c_str());

		const auto ntstatus = worker->OnSvcStart(argWorkDir, FileSystem);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: OnSvcStart");
			return ntstatus;
		}
	}

	ICSService* services[] = { mCSDevice };

	for (int i=0; i<_countof(services); i++)
	{
		const auto service = services[i];
		const auto klassName = getDerivedClassNamesW(service);

		traceW(L"%s::OnSvcStart()", klassName.c_str());

		const auto ntstatus = services[i]->OnSvcStart(argWorkDir, FileSystem);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: OnSvcStart[%d]", i);
			return ntstatus;
		}
	}

	return STATUS_SUCCESS;
}

VOID CSDriver::OnSvcStop()
{
	StatsIncr(OnSvcStop);

	NEW_LOG_BLOCK();

	// OnSvcStop() の伝播

	for (const auto& it: mWorkers)
	{
		const auto worker = it.second;
		const auto klassName = getDerivedClassNamesW(worker);

		traceW(L"%s::OnSvcStop()", klassName.c_str());

		worker->OnSvcStop();
	}

	ICSService* services[] = { mCSDevice };

	for (int i=0; i<_countof(services); i++)
	{
		const auto service = services[i];
		const auto klassName = getDerivedClassNamesW(service);

		traceW(L"%s::OnSvcStop()", klassName.c_str());

		services[i]->OnSvcStop();
	}
}

// EOF
