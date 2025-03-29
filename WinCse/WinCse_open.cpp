#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>
#include <sstream>

using namespace WinCseLib;


NTSTATUS WinCse::DoCreate(const wchar_t* FileName,
	UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoCreate);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!mReadonlyVolume);		// �����炭�V�F���ō폜���삪�~�߂��Ă���

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"CreateOptions=%u, GrantedAccess=%u, FileAttributes=%u, SecurityDescriptor=%p, AllocationSize=%llu, PFileContext=%p, FileInfo=%p",
		CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

	if (isFileNameIgnored(FileName))
	{
		// "desktop.ini" �Ȃǂ͖���������

		traceW(L"ignore pattern");
		return STATUS_OBJECT_NAME_INVALID;
	}

	const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
	if (objKey.invalid())
	{
		traceW(L"illegal FileName: \"%s\"", FileName);

		return STATUS_OBJECT_NAME_INVALID;
	}

	traceW(L"objKey=%s", objKey.str().c_str());

	if (objKey.isBucket())
	{
		// �o�P�b�g�ɑ΂��� create �̎��s

		//return STATUS_ACCESS_DENIED;
		//return FspNtStatusFromWin32(ERROR_ACCESS_DENIED);
		//return FspNtStatusFromWin32(ERROR_WRITE_PROTECT);
		return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
	}

	APP_ASSERT(objKey.hasKey());

	if (CreateOptions & FILE_DIRECTORY_FILE)
	{
		// "�f�B���N�g��" �̂Ƃ�

		if (mCSDevice->headObject(START_CALLER objKey, nullptr))
		{
			// �������O�� "�t�@�C��" �����݂���

			return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		}
	}
	else
	{
		// "�t�@�C��" �̂Ƃ�

		if (mCSDevice->headObject(START_CALLER objKey.toDir(), nullptr))
		{
			// �������O�� "�f�B���N�g��" �����݂���

			return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		}
	}

	const ObjectKey createObjKey{ CreateOptions & FILE_DIRECTORY_FILE ? objKey.toDir() : objKey };

	if (mCSDevice->headObject(START_CALLER createObjKey, nullptr))
	{
		// �������O�̂��̂����݂���Ƃ�

		traceW(L"fault: exists same name");

		return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
	}

	//
	// ��̃I�u�W�F�N�g���쐬���āA���̏��� fileInfo �ɋL�^����
	//

	FSP_FSCTL_FILE_INFO fileInfo{};

	if (!mCSDevice->putObject(START_CALLER createObjKey, nullptr, &fileInfo))
	{
		traceW(L"fault: putObject");

		//return STATUS_DEVICE_NOT_READY;
		return FspNtStatusFromWin32(ERROR_IO_DEVICE);
	}

	if (CreateOptions & FILE_DIRECTORY_FILE)
	{
		// go next
	}
	else
	{
		// �t�@�C���̏ꍇ

		FSP_FSCTL_FILE_INFO checkFileInfo{};

		if (!mCSDevice->headObject(START_CALLER createObjKey, &checkFileInfo))
		{
			traceW(L"fault: putObject");

			return FspNtStatusFromWin32(ERROR_IO_DEVICE);
		}

		if (fileInfo.CreationTime == checkFileInfo.CreationTime &&
			fileInfo.LastAccessTime == checkFileInfo.LastAccessTime &&
			fileInfo.LastWriteTime == checkFileInfo.LastWriteTime &&
			fileInfo.FileSize == checkFileInfo.FileSize)
		{
			// go next
		}
		else
		{
			// putObject() ������ԂƈقȂ��Ă���̂ŁA���ŃA�b�v���[�h�������̂Ɣ���
			// --> ���̃v���Z�X�ɂ���ăt�@�C�����g�p��

			return FspNtStatusFromWin32(ERROR_SHARING_VIOLATION);
		}
	}

	//
	NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;
	CSDeviceContext* ctx = nullptr;

	PTFS_FILE_CONTEXT* FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
	if (0 == FileContext)
	{
		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->FileName = _wcsdup(FileName);
	if (!FileContext->FileName)
	{
		traceW(L"fault: _wcsdup");

		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	// ���\�[�X���쐬�� UParam �ɕۑ�

	StatsIncr(_CallCreate);

	ctx = mCSDevice->create(START_CALLER createObjKey, fileInfo,
		CreateOptions, GrantedAccess, FileAttributes);

	if (!ctx)
	{
		traceW(L"fault: create");

		//ntstatus = STATUS_DEVICE_NOT_READY;
		ntstatus = FspNtStatusFromWin32(ERROR_IO_DEVICE);
		goto exit;
	}

	FileContext->UParam = ctx;

	FileContext->FileInfo = fileInfo;

	// �S�~����Ώۂɓo�^
	mResourceSweeper.add(FileContext);

	*PFileContext = FileContext;
	FileContext = nullptr;

	*FileInfo = fileInfo;

	ntstatus = STATUS_SUCCESS;

exit:
	if (FileContext)
	{
		free(FileContext->FileName);
	}
	free(FileContext);

	traceW(L"return NTSTATUS=%ld", ntstatus);

	return ntstatus;
}

NTSTATUS WinCse::DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOpen);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName && PFileContext && FileInfo);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!isFileNameIgnored(FileName));

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", CreateOptions, GrantedAccess, PFileContext, FileInfo);

	FSP_FSCTL_FILE_INFO fileInfo{};

	NTSTATUS ntstatus = FileNameToFileInfo(START_CALLER FileName, &fileInfo);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: FileNameToFileInfo");
		return ntstatus;
	}

	if (!FA_IS_DIR(fileInfo.FileAttributes))
	{
		// �t�@�C���̍ő�T�C�Y�m�F

		traceW(L"FileSize: %llu", fileInfo.FileSize);

		if (mMaxFileSize > 0)
		{
			if (fileInfo.FileSize > (FILESIZE_1MiBu * mMaxFileSize))
			{
				traceW(L"%llu: When a file size exceeds the maximum size that can be opened.", fileInfo.FileSize);

				//return STATUS_DEVICE_NOT_READY;
				return FspNtStatusFromWin32(ERROR_IO_DEVICE);
			}
		}
	}

	// �O�̂��ߌ���
	APP_ASSERT(fileInfo.LastWriteTime);

	// WinFsp �ɕۑ������t�@�C���E�R���e�L�X�g�𐶐�

	PTFS_FILE_CONTEXT* FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
	if (!FileContext)
	{
		traceW(L"fault: calloc");

		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->FileName = _wcsdup(FileName);
	if (!FileContext->FileName)
	{
		traceW(L"fault: _wcsdup");

		ntstatus = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->FileInfo = fileInfo;

	if (wcscmp(FileName, L"\\") == 0)
	{
		traceW(L"root access");
	}
	else
	{
		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (objKey.invalid())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);

			ntstatus = STATUS_OBJECT_NAME_INVALID;
			goto exit;
		}

		// �N���E�h�E�X�g���[�W�̃R���e�L�X�g�� UParam �ɕۑ�������

		StatsIncr(_CallOpen);

		CSDeviceContext* ctx = mCSDevice->open(START_CALLER objKey, CreateOptions, GrantedAccess, FileContext->FileInfo);
		if (!ctx)
		{
			traceW(L"fault: open");

			//ntstatus = STATUS_DEVICE_NOT_READY;
			ntstatus = FspNtStatusFromWin32(ERROR_IO_DEVICE);
			goto exit;
		}

		FileContext->UParam = ctx;
	}

	// �S�~����Ώۂɓo�^
	mResourceSweeper.add(FileContext);

	*PFileContext = FileContext;
	FileContext = nullptr;

	*FileInfo = fileInfo;

	ntstatus = STATUS_SUCCESS;

exit:
	if (FileContext)
	{
		free(FileContext->FileName);
	}
	free(FileContext);

	traceW(L"return NTSTATUS=%ld", ntstatus);

	return ntstatus;
}

VOID WinCse::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"(FileContext)FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Flags=%lu", Flags);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		if (Flags & FspCleanupDelete)
		{
			// setDelete() �ɂ��폜�t���O��ݒ肳�ꂽ�t�@�C���ƁA
			// CreateFile() ���� FILE_FLAG_DELETE_ON_CLOSE �̑������^����ꂽ�t�@�C��
			// ���N���[�Y�����Ƃ��ɂ�����ʉ߂���

			bool b = mCSDevice->deleteObject(START_CALLER ctx->mObjKey);
			if (!b)
			{
				traceW(L"fault: deleteObject");
			}

			// WinFsp �� Cleanup() �� CloseHandle() ���Ă���̂ŁA���l�̏������s��

			ctx->mFile.close();
		}
	}
}

NTSTATUS WinCse::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	StatsIncr(DoClose);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"UParam=%p", FileContext->UParam);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		APP_ASSERT(wcscmp(FileContext->FileName, L"\\") != 0);

		// �N���E�h�E�X�g���[�W�� UParam �����������

		StatsIncr(_CallClose);
		mCSDevice->close(START_CALLER ctx);
	}

	free(FileContext->FileName);

	FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);

	// �S�~����Ώۂ���폜
	mResourceSweeper.remove(FileContext);

	free(FileContext);

	return STATUS_SUCCESS;
}

// EOF