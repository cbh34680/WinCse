#include "WinCseLib.h"
#include "CSDriver.hpp"
#include <filesystem>
#include <iostream>

using namespace WCSE;


static PCWSTR CONFIGFILE_FNAME = L"WinCse.conf";

//
// �v���O�������� "-u" ����Z�o���ꂽ�f�B���N�g������ ini �t�@�C����ǂ�
// S3 �N���C�A���g�𐶐�����
//
NTSTATUS CSDriver::PreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
	StatsIncr(PreCreateFilesystem);

	NEW_LOG_BLOCK();
	APP_ASSERT(argWorkDir);
	APP_ASSERT(VolumeParams);

	traceW(L"argWorkDir=%s", argWorkDir);

	namespace fs = std::filesystem;

	APP_ASSERT(fs::exists(argWorkDir));
	APP_ASSERT(fs::is_directory(argWorkDir));

	// VolumeParams �̐ݒ�

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
	// ini �t�@�C������l���擾
	//
	const auto workDir{ fs::path(argWorkDir).wstring() };
	const auto confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

	traceW(L"confPath=%s", confPath.c_str());

	// �ǂݎ���p

	const bool readonly = ::GetPrivateProfileIntW(mIniSection.c_str(), L"readonly", 0, confPath.c_str()) != 0;
	if (readonly)
	{
		// �{�����[���̐ݒ�

		VolumeParams->ReadOnlyVolume = 1;
	}

	// PreCreateFilesystem() �̓`�d

	for (const auto& it: mWorkers)
	{
		const auto worker = it.second;
		const auto klassName{ getDerivedClassNamesW(worker) };

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
		const auto klassName{ getDerivedClassNamesW(service) };

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


	// ��������t�@�C�����̃p�^�[��

	std::wstring re_ignore_patterns;
	GetIniStringW(confPath.c_str(), mIniSection.c_str(), L"re_ignore_patterns", &re_ignore_patterns);

	if (!re_ignore_patterns.empty())
	{
		try
		{
			// conf �Ŏw�肳�ꂽ���K�\���p�^�[���̐������e�X�g
			// �s���ȃp�^�[���̏ꍇ�͗�O�� catch �����̂Ŕ��f����Ȃ�

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
	// �����Q�Ɨp�t�@�C��/�f�B���N�g���̏���
	//
	mRefFile = ::CreateFileW
	(
		confPath.c_str(),
		FILE_READ_ATTRIBUTES | READ_CONTROL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,		// ���L���[�h
		NULL,														// �Z�L�����e�B����
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL														// �e���v���[�g�Ȃ�
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

	// �����o�ɕۑ�

	//mWorkDir = workDir;
	//traceW(L"INFO: WorkDir=%s", mWorkDir.c_str());

	// OnSvcStart() �̓`�d

	for (const auto& it: mWorkers)
	{
		const auto worker = it.second;
		const auto klassName{ getDerivedClassNamesW(worker) };

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
		const auto klassName{ getDerivedClassNamesW(service) };

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

	// OnSvcStop() �̓`�d

	for (const auto& it: mWorkers)
	{
		const auto worker = it.second;
		const auto klassName{ getDerivedClassNamesW(worker) };

		traceW(L"%s::OnSvcStop()", klassName.c_str());

		worker->OnSvcStop();
	}

	ICSService* services[] = { mCSDevice };

	for (int i=0; i<_countof(services); i++)
	{
		const auto service = services[i];
		const auto klassName{ getDerivedClassNamesW(service) };

		traceW(L"%s::OnSvcStop()", klassName.c_str());

		services[i]->OnSvcStop();
	}
}

// EOF
