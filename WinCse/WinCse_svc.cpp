#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>
#include <iostream>

using namespace WinCseLib;

static const wchar_t* CONFIGFILE_FNAME = L"WinCse.conf";
static const wchar_t* FILE_REFERENCE_FNAME = L"reference.file";
static const wchar_t* DIR_REFERENCE_FNAME = L"reference.dir";

template <typename T>
std::string getDerivedClassNames(T* baseClass)
{
	const std::type_info& typeInfo = typeid(*baseClass);
	return typeInfo.name();
}

//
// プログラム引数 "-u" から算出されたディレクトリから ini ファイルを読み
// S3 クライアントを生成する
//
bool WinCse::OnSvcStart(const wchar_t* argWorkDir)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);

	namespace fs = std::filesystem;

	APP_ASSERT(fs::exists(argWorkDir));
	APP_ASSERT(fs::is_directory(argWorkDir));

	bool ret = false;

	try
	{
		const std::wstring workDir{ fs::weakly_canonical(fs::path(argWorkDir)).wstring() };

		//
		// ini ファイルから値を取得
		//
		const std::wstring confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

		traceW(L"Detect credentials file path is %s", confPath.c_str());

		const auto iniSection = mIniSection.c_str();

		//
		// 最大ファイルサイズ(MB)
		//
		const int maxFileSize = (int)::GetPrivateProfileIntW(iniSection, L"max_filesize_mb", 4, confPath.c_str());

		//
		// 属性参照用ファイル/ディレクトリの準備
		//
		const std::wstring fileRefPath{ mTempDir + L'\\' + FILE_REFERENCE_FNAME };
		if (!TouchIfNotExists(fileRefPath))
		{
			traceW(L"file not exists: %s", fileRefPath.c_str());
			return false;
		}

		const std::wstring dirRefPath{ mTempDir + L'\\' + DIR_REFERENCE_FNAME };
		if (!MkdirIfNotExists(dirRefPath))
		{
			traceW(L"dir not exists: %s", dirRefPath.c_str());
			return false;
		}

		//
		// 属性参照用ファイル/ディレクトリを開く
		//
		mFileRefHandle = ::CreateFileW(fileRefPath.c_str(),
			FILE_READ_ATTRIBUTES | READ_CONTROL, 0, 0,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
		if (INVALID_HANDLE_VALUE == mFileRefHandle)
		{
			traceW(L"file open error: %s", fileRefPath.c_str());
			return false;
		}

		mDirRefHandle = ::CreateFileW(dirRefPath.c_str(),
			FILE_READ_ATTRIBUTES | READ_CONTROL, 0, 0,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
		if (INVALID_HANDLE_VALUE == mDirRefHandle)
		{
			traceW(L"file open error: %s", dirRefPath.c_str());
			return false;
		}

		mMaxFileSize = maxFileSize;
		mWorkDir = workDir;

		traceW(L"INFO: TempDir=%s, WorkDir=%s, DirRef=%s, FileRef=%s",
			mTempDir.c_str(), mWorkDir.c_str(), dirRefPath.c_str(), fileRefPath.c_str());

		IService* services[] = { mDelayedWorker, mIdleWorker, mStorage};

		// OnSvcStart() の伝播
		for (int i=0; i<_countof(services); i++)
		{
			const auto service = services[i];
			const auto className = getDerivedClassNames(service);

			traceA("%s::OnSvcStart()", className.c_str());

			if (!services[i]->OnSvcStart(argWorkDir))
			{
				traceA("fault: OnSvcStart()");
				return false;
			}
		}

		// OnPostSvcStart() の伝播
		for (int i=0; i<_countof(services); i++)
		{
			const auto service = services[i];
			const auto className = getDerivedClassNames(service);

			traceA("%s::OnPostSvcStart()", className.c_str());

			if (!services[i]->OnPostSvcStart())
			{
				traceA("fault: OnPostSvcStart()");
				return false;
			}
		}

		ret = true;
	}
	catch (const std::runtime_error& err)
	{
		std::cerr << "what: " << err.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << "unknown error" << std::endl;
	}

	return ret;		// 例外発生時に false
}

void WinCse::OnSvcStop()
{
	NEW_LOG_BLOCK();

	// ワーカー・スレッドの停止
	mDelayedWorker->OnSvcStop();
	mIdleWorker->OnSvcStop();

	// ストレージの終了
	mStorage->OnSvcStop();
}

// EOF
