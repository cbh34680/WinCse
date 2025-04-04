#include "WinCseLib.h"
#include "CSDriver.hpp"
#include <filesystem>
#include <sstream>

using namespace WCSE;


NTSTATUS CSDriver::DoCreate(const wchar_t* FileName,
	UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoCreate);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!mReadonlyVolume);		// �����炭�V�F���ō폜���삪�~�߂��Ă���

	traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, FileAttributes=%u, SecurityDescriptor=%p, AllocationSize=%llu, PFileContext=%p, FileInfo=%p",
		FileName, CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

	if (shouldIgnoreFileName(FileName))
	{
		// "desktop.ini" �Ȃǂ͖���������

		traceW(L"ignore pattern");
		return STATUS_OBJECT_NAME_INVALID;
	}

	const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
	if (objKey.invalid())
	{
		traceW(L"invalid FileName=%s", FileName);

		return STATUS_OBJECT_NAME_INVALID;
	}

	traceW(L"objKey=%s", objKey.str().c_str());

	if (objKey.isBucket())
	{
		// �o�P�b�g�ɑ΂��� create �̎��s

		//return STATUS_ACCESS_DENIED;
		//return FspNtStatusFromWin32(ERROR_ACCESS_DENIED);
		//return FspNtStatusFromWin32(ERROR_WRITE_PROTECT);
		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	APP_ASSERT(objKey.hasKey());

	if (CreateOptions & FILE_DIRECTORY_FILE)
	{
		// "�f�B���N�g��" �̂Ƃ�
		// 
		// --> �����̃t�@�C��������

		if (mCSDevice->headObject_File(START_CALLER objKey, nullptr))
		{
			// �������O�� "�t�@�C��" �����݂���

			traceW(L"fault: exists same name");
			return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		}
	}
	else
	{
		// "�t�@�C��" �̂Ƃ�
		//
		// --> �����̃f�B���N�g��������

		if (mCSDevice->headObject_Dir(START_CALLER objKey.toDir(), nullptr))
		{
			// �������O�� "�f�B���N�g��" �����݂���

			traceW(L"fault: exists same name");
			return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		}
	}

	const ObjectKey createObjKey{ CreateOptions & FILE_DIRECTORY_FILE ? objKey.toDir() : objKey };

	if (mCSDevice->headObject(START_CALLER createObjKey, nullptr))
	{
		// �������O�̂��̂����݂���Ƃ�

		traceW(L"fault: exists same name");
		//return FspNtStatusFromWin32(ERROR_FILE_EXISTS);
		return STATUS_OBJECT_NAME_COLLISION;				// https://github.com/winfsp/winfsp/issues/601
	}

	//
	// ��̃I�u�W�F�N�g���쐬���āA���̏��� fileInfo �ɋL�^����
	//

	const auto now = GetCurrentWinFileTime100ns();

	FSP_FSCTL_FILE_INFO fileInfo{};
	fileInfo.FileAttributes = FileAttributes;
	fileInfo.CreationTime = now;
	fileInfo.LastAccessTime = now;
	fileInfo.LastWriteTime = now;

	if (!mCSDevice->putObject(START_CALLER createObjKey, fileInfo, nullptr))
	{
		traceW(L"fault: putObject");

		//return STATUS_DEVICE_NOT_READY;
		return FspNtStatusFromWin32(ERROR_IO_DEVICE);
	}

	if (CreateOptions & FILE_DIRECTORY_FILE)
	{
		// �f�B���N�g���̏ꍇ
		//
		// --> �N����������̂ł��C�ɂ���K�v�͂Ȃ�
	}
	else
	{
		// �t�@�C���̏ꍇ

		FSP_FSCTL_FILE_INFO checkFileInfo;

		if (!mCSDevice->headObject_File(START_CALLER createObjKey, &checkFileInfo))
		{
			traceW(L"fault: headObject_File");
			return FspNtStatusFromWin32(ERROR_IO_DEVICE);
		}

		if (fileInfo.CreationTime == checkFileInfo.CreationTime &&
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

NTSTATUS CSDriver::DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOpen);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName && PFileContext && FileInfo);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!shouldIgnoreFileName(FileName));

	//traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);

	FSP_FSCTL_FILE_INFO fileInfo{};

	NTSTATUS ntstatus = FileNameToFileInfo(START_CALLER FileName, &fileInfo);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: FileNameToFileInfo, FileName=%s", FileName);
		return ntstatus;
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
		// go next
	}
	else
	{
		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (objKey.invalid())
		{
			traceW(L"invalid FileName=%s", FileName);

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

		if (!ctx->mObjKey.isBucket())
		{
			traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);
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

	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"return NTSTATUS=%ld", ntstatus);
	}

	return ntstatus;
}

VOID CSDriver::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		traceW(L"FileName=%s, Flags=%lu", FileName, Flags);

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
			//
			// --> ������ close() ���Ă������ƂŁA�A�b�v���[�h���K�v�����ȒP�ɔ��f�ł���

			ctx->mFile.close();
		}
	}
}

NTSTATUS CSDriver::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	StatsIncr(DoClose);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		APP_ASSERT(wcscmp(FileContext->FileName, L"\\") != 0);

		//if (!ctx->mObjKey.isBucket())
		if (ctx->isFile())
		{
			traceW(L"FileName=%s, UParam=%p", FileContext->FileName, FileContext->UParam);
		}

		// �N���E�h�E�X�g���[�W�� UParam �����������

		StatsIncr(_CallClose);
		mCSDevice->close(START_CALLER ctx);

		delete ctx;
	}

	free(FileContext->FileName);

	FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);

	// �S�~����Ώۂ���폜
	mResourceSweeper.remove(FileContext);

	free(FileContext);

	return STATUS_SUCCESS;
}

// EOF