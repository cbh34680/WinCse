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

	traceW(L"FileName: \"%s\"", FileName);
	traceW(L"CreateOptions=%u, GrantedAccess=%u, FileAttributes=%u, SecurityDescriptor=%p, AllocationSize=%llu, PFileContext=%p, FileInfo=%p",
		CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

	const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
	NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;
	PTFS_FILE_CONTEXT* FileContext = nullptr;
	FSP_FSCTL_FILE_INFO fileInfo{};

	if (mReadonlyVolume)
	{
		// �����͒ʉ߂��Ȃ�
		// �����炭�V�F���ō폜���삪�~�߂��Ă���

		traceW(L"readonly volume");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (isFileNameIgnored(FileName))
	{
		ntstatus = STATUS_OBJECT_NAME_INVALID;
		goto exit;
	}

	if (!objKey.valid())
	{
		traceW(L"illegal FileName: \"%s\"", FileName);
		ntstatus = STATUS_OBJECT_NAME_INVALID;
		goto exit;
	}

	traceW(L"objKey=%s", objKey.str().c_str());

	if (!mCSDevice->headBucket(START_CALLER objKey.bucket()))
	{
		// "\\" �ɐV�K�쐬���悤�Ƃ����Ƃ�

		traceW(L"fault: headBucket");
		goto exit;

		//return STATUS_ACCESS_DENIED;				// ���z�I�ȃ��b�Z�[�W�����A���x���Ăяo�����
		//return STATUS_INVALID_PARAMETER;			// �ςȃ��b�Z�[�W�ɂȂ�
		//return STATUS_NOT_IMPLEMENTED;			// ������ MS-DOS �t�@���N�V���� (���x���Ăяo����Ă��܂�)
	}

	//
	FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
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

	if (objKey.hasKey())
	{
		// �N���E�h�E�X�g���[�W�̃R���e�L�X�g�� UParam �ɕۑ�������

		StatsIncr(_CallCreate);

		CSDeviceContext* ctx = mCSDevice->create(START_CALLER objKey, CreateOptions, GrantedAccess, FileAttributes, &fileInfo);
		if (!ctx)
		{
			traceW(L"fault: openFile");
			ntstatus = STATUS_DEVICE_NOT_READY;
			goto exit;
		}

		FileContext->UParam = ctx;
	}
	else
	{
		// "\\bucket" �ɑ΂��� create --> �f�B���N�g��

		ntstatus = GetFileInfoInternal(mRefDir.handle(), &fileInfo);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: GetFileInfoInternal");
			goto exit;
		}
	}

	FileContext->FileInfo = fileInfo;

	// �S�~����Ώۂɓo�^
	mResourceRAII.add(FileContext);

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


// EOF