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
bool CSDriver::PreCreateFilesystem(FSP_SERVICE *Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
	StatsIncr(PreCreateFilesystem);

	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(VolumeParams);

	namespace fs = std::filesystem;

	APP_ASSERT(fs::exists(argWorkDir));
	APP_ASSERT(fs::is_directory(argWorkDir));

	bool ret = false;

	try
	{
		// VolumeParams の設定

		VolumeParams->CaseSensitiveSearch = 1;

		const UINT32 Timeout = 2000U;

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

		const std::wstring workDir{ fs::weakly_canonical(fs::path(argWorkDir)).wstring() };

		//
		// ini ファイルから値を取得
		//
		const std::wstring confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

		traceW(L"Detect credentials file path is %s", confPath.c_str());

		const auto iniSection = mIniSection.c_str();

		// 読み取り専用

		const bool readonly = ::GetPrivateProfileIntW(iniSection, L"readonly", 0, confPath.c_str()) != 0;
		if (readonly)
		{
			// ボリュームの設定

			VolumeParams->ReadOnlyVolume = 1;
		}

		// 無視するファイル名のパターン

		std::wstring re_ignore_patterns;
		GetIniStringW(confPath.c_str(), iniSection, L"re_ignore_patterns", &re_ignore_patterns);

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
			traceW(L"file open error: %s", confPath.c_str());
			return false;
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
			traceW(L"file open error: %s", argWorkDir);
			return false;
		}

		// メンバに保存

		mWorkDir = workDir;

		traceW(L"INFO: TempDir=%s, WorkDir=%s", mTempDir.c_str(), mWorkDir.c_str());

		// PreCreateFilesystem() の伝播

		for (const auto& it: mWorkers)
		{
			const auto worker = it.second;
			const auto klassName = getDerivedClassNamesW(worker);

			traceW(L"%s::PreCreateFilesystem()", klassName.c_str());

			if (!worker->PreCreateFilesystem(Service, argWorkDir, VolumeParams))
			{
				traceW(L"fault: PreCreateFilesystem");
				return false;
			}
		}

		ICSService* services[] = { mCSDevice };

		for (int i=0; i<_countof(services); i++)
		{
			const auto service = services[i];
			const auto klassName = getDerivedClassNamesW(service);

			traceW(L"%s::PreCreateFilesystem()", klassName.c_str());

			if (!services[i]->PreCreateFilesystem(Service, argWorkDir, VolumeParams))
			{
				traceW(L"fault: PreCreateFilesystem");
				return false;
			}
		}

		ret = true;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "what: " << ex.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << "unknown error" << std::endl;
	}

	return ret;		// 例外発生時に false
}

bool CSDriver::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem, PCWSTR PtfsPath)
{
	StatsIncr(OnSvcStart);

	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(FileSystem);

	bool ret = false;

	try
	{
		// OnSvcStart() の伝播

		for (const auto& it: mWorkers)
		{
			const auto worker = it.second;
			const auto klassName = getDerivedClassNamesW(worker);

			traceW(L"%s::OnSvcStart()", klassName.c_str());

			if (!worker->OnSvcStart(argWorkDir, FileSystem, PtfsPath))
			{
				traceW(L"fault: PreCreateFilesystem");
				return false;
			}
		}

		ICSService* services[] = { mCSDevice };

		for (int i=0; i<_countof(services); i++)
		{
			const auto service = services[i];
			const auto klassName = getDerivedClassNamesW(service);

			traceW(L"%s::OnSvcStart()", klassName.c_str());

			if (!services[i]->OnSvcStart(argWorkDir, FileSystem, PtfsPath))
			{
				traceW(L"fault: OnSvcStart");
				return false;
			}
		}

		ret = true;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "what: " << ex.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << "unknown error" << std::endl;
	}

	return ret;
}

void CSDriver::OnSvcStop()
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
