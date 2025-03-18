#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>
#include <sstream>

using namespace WinCseLib;


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

	PTFS_FILE_CONTEXT* FileContext = nullptr;
	FSP_FSCTL_FILE_INFO fileInfo{};
	NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

	ntstatus = FileNameToFileInfo(START_CALLER FileName, &fileInfo);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: FileNameToFileInfo");
		goto exit;
	}

	// �O�̂��ߌ���
	APP_ASSERT(fileInfo.LastWriteTime);

	// WinFsp �ɕۑ������t�@�C���E�R���e�L�X�g�𐶐�

	FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof(*FileContext));
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

	if (wcscmp(FileName, L"\\") == 0)
	{
		traceW(L"root access");

		//APP_ASSERT(fileInfo.FileSize == 0);
	}
	else
	{
		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };

		if (!objKey.valid())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);
			ntstatus = STATUS_OBJECT_NAME_INVALID;
			goto exit;
		}

		if (FA_IS_DIR(fileInfo.FileAttributes))
		{
			// �f�B���N�g���ւ̃A�N�Z�X

			//APP_ASSERT(fileInfo.FileSize == 0);
		}
		else
		{
			// �t�@�C���ւ̃A�N�Z�X

			APP_ASSERT(objKey.hasKey());

			traceW(L"FileSize: %llu", fileInfo.FileSize);

			if (mMaxFileSize > 0)
			{
				if (fileInfo.FileSize > 1024ULL * 1024 * mMaxFileSize)
				{
					ntstatus = STATUS_DEVICE_NOT_READY;
					traceW(L"%llu: When a file size exceeds the maximum size that can be opened.", fileInfo.FileSize);
					goto exit;
				}
			}
		}

		// �N���E�h�E�X�g���[�W�̃R���e�L�X�g�� UParam �ɕۑ�������

		StatsIncr(_CallOpen);

		CSDeviceContext* ctx = mCSDevice->open(START_CALLER objKey, CreateOptions, GrantedAccess, fileInfo);
		if (!ctx)
		{
			traceW(L"fault: openFile");
			ntstatus = STATUS_DEVICE_NOT_READY;
			goto exit;
		}

		FileContext->UParam = ctx;
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
	mResourceRAII.del(FileContext);

	free(FileContext);

	return STATUS_SUCCESS;
}

//
// �G�N�X�v���[���[���J�����܂ܐؒf����� WinFsp �� Close �����s����Ȃ� (�ׂ��Ǝv��)
// �̂ŁADoOpen ���Ă΂�� DoClose ���Ă΂�Ă��Ȃ����̂́A�A�v���P�[�V�����I������
// �����I�� DoClose ���Ăяo��
// 
// ���u���Ă����͂Ȃ����A�f�o�b�O���Ƀ��������[�N�Ƃ��ĕ񍐂���Ă��܂�
// �{���̈Ӗ��ł̃��������[�N�ƍ��݂��Ă��܂�����
//
WinCse::ResourceRAII::~ResourceRAII()
{
	NEW_LOG_BLOCK();

	// DoClose �� mOpenAddrs.erase() ������̂ŃR�s�[���K�v

	auto copy{ mOpenAddrs };

	for (auto& FileContext: copy)
	{
		::_InterlockedIncrement(&(mThat->mStats->_ForceClose));

		traceW(L"force close address=%p", FileContext);

		mThat->DoClose(FileContext);
	}
}

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

void WinCse::ResourceRAII::add(PTFS_FILE_CONTEXT* FileContext)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	APP_ASSERT(mOpenAddrs.find(FileContext) == mOpenAddrs.end());

	traceW(L"add address=%p", FileContext);

	mOpenAddrs.insert(FileContext);
}

void WinCse::ResourceRAII::del(PTFS_FILE_CONTEXT* FileContext)
{
	THREAD_SAFE();
	NEW_LOG_BLOCK();
	auto it{ mOpenAddrs.find(FileContext) };
	APP_ASSERT(it != mOpenAddrs.end());

	traceW(L"remove address=%p", FileContext);

	mOpenAddrs.erase(FileContext);
}

// EOF