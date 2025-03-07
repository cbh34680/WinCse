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
// �v���O�������� "-u" ����Z�o���ꂽ�f�B���N�g������ ini �t�@�C����ǂ�
// S3 �N���C�A���g�𐶐�����
//
bool WinCse::PreCreateFilesystem(const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
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
		const std::wstring workDir{ fs::weakly_canonical(fs::path(argWorkDir)).wstring() };

		//
		// ini �t�@�C������l���擾
		//
		const std::wstring confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

		traceW(L"Detect credentials file path is %s", confPath.c_str());

		const auto iniSection = mIniSection.c_str();

		//
		// �ő�t�@�C���T�C�Y(MB)
		//
		const int maxFileSize = (int)::GetPrivateProfileIntW(iniSection, L"max_filesize_mb", 4, confPath.c_str());

		//
		// ��������t�@�C�����̃p�^�[��
		//
		std::wstring re_ignored_patterns;
		GetIniStringW(confPath.c_str(), iniSection, L"re_ignored_patterns", &re_ignored_patterns);

		if (!re_ignored_patterns.empty())
		{
			try
			{
				// conf �Ŏw�肳�ꂽ���K�\���p�^�[���̐������e�X�g
				// �s���ȃp�^�[���̏ꍇ�͗�O�� catch �����̂Ŕ��f����Ȃ�

				std::wregex reTest{ re_ignored_patterns, std::regex_constants::icase };

				// OK
				mIgnoredFileNamePatterns = std::move(reTest);
			}
			catch (const std::regex_error& ex)
			{
				traceA("regex_error: %s", ex.what());
				traceW(L"%s: ignored, set default patterns", re_ignored_patterns.c_str());
			}
		}

		//
		// �����Q�Ɨp�t�@�C��/�f�B���N�g���̏���
		//
		const std::wstring fileRefPath{ workDir + L'\\' + FILE_REFERENCE_FNAME };
		if (!TouchIfNotExists(fileRefPath))
		{
			traceW(L"file not exists: %s", fileRefPath.c_str());
			return false;
		}

		const std::wstring dirRefPath{ workDir + L'\\' + DIR_REFERENCE_FNAME };
		if (!MkdirIfNotExists(dirRefPath))
		{
			traceW(L"dir not exists: %s", dirRefPath.c_str());
			return false;
		}

		//
		// �����Q�Ɨp�t�@�C��/�f�B���N�g�����J��
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

		ICSService* services[] = { mDelayedWorker, mIdleWorker, mCSDevice };

		// PreCreateFilesystem() �̓`�d
		for (int i=0; i<_countof(services); i++)
		{
			const auto service = services[i];
			const auto klassName = getDerivedClassNames(service);

			traceA("%s::PreCreateFilesystem()", klassName.c_str());

			if (!services[i]->PreCreateFilesystem(argWorkDir, VolumeParams))
			{
				traceA("fault: PreCreateFilesystem");
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

	return ret;		// ��O�������� false
}

bool WinCse::OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
	StatsIncr(OnSvcStart);

	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(FileSystem);

	bool ret = false;

	try
	{
		ICSService* services[] = { mDelayedWorker, mIdleWorker, mCSDevice };

		// OnSvcStart() �̓`�d
		for (int i=0; i<_countof(services); i++)
		{
			const auto service = services[i];
			const auto klassName = getDerivedClassNames(service);

			traceA("%s::OnSvcStart()", klassName.c_str());

			if (!services[i]->OnSvcStart(argWorkDir, FileSystem))
			{
				traceA("fault: OnSvcStart");
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

void WinCse::OnSvcStop()
{
	StatsIncr(OnSvcStop);

	NEW_LOG_BLOCK();

	ICSService* services[] = { mDelayedWorker, mIdleWorker, mCSDevice };

	// OnSvcStop() �̓`�d
	for (int i=0; i<_countof(services); i++)
	{
		const auto service = services[i];
		const auto klassName = getDerivedClassNames(service);

		traceA("%s::OnSvcStop()", klassName.c_str());

		services[i]->OnSvcStop();
	}
}

// EOF
