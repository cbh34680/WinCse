#include "CSDriver.hpp"
#include <filesystem>

using namespace WCSE;


NTSTATUS CSDriver::DoOpen(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoOpen);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName && PFileContext && FileInfo);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!this->shouldIgnoreFileName(FileName));

	//traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);

	FSP_FSCTL_FILE_INFO fileInfo;
	FileNameType fileNameType;

	const auto ntstatus = this->getFileInfoByFileName(START_CALLER FileName, &fileInfo, &fileNameType);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: getFileInfoByFileName, FileName=%s", FileName);
		return ntstatus;
	}

	// �O�̂��ߌ���
	APP_ASSERT(fileInfo.LastWriteTime);

	// WinFsp �ɕۑ������t�@�C���E�R���e�L�X�g�𐶐�

	auto fc{ std::unique_ptr<PTFS_FILE_CONTEXT, void(*)(void*)>((PTFS_FILE_CONTEXT*)calloc(1, sizeof(PTFS_FILE_CONTEXT)), free) };
	if (!fc)
	{
		traceW(L"fault: calloc");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	auto fn{ std::unique_ptr<wchar_t, void(*)(void*)>(_wcsdup(FileName), free) };
	if (!fn)
	{
		traceW(L"fault: _wcsdup");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PTFS_FILE_CONTEXT* FileContext = fc.get();
	FileContext->FileName = fn.get();
	FileContext->FileInfo = fileInfo;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// go next

		APP_ASSERT(fileNameType == FileNameType::RootDirectory);
	}
	else
	{
		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (objKey.invalid())
		{
			traceW(L"invalid FileName=%s", FileName);

			return STATUS_OBJECT_NAME_INVALID;
		}

		// �N���E�h�E�X�g���[�W�̃R���e�L�X�g�� UParam �ɕۑ�������

		StatsIncr(_CallOpen);

		CSDeviceContext* ctx = mCSDevice->open(START_CALLER objKey, CreateOptions, GrantedAccess, FileContext->FileInfo);
		if (!ctx)
		{
			traceW(L"fault: open");

			//return STATUS_DEVICE_NOT_READY;
			return FspNtStatusFromWin32(ERROR_IO_DEVICE);
		}

		if (ctx->mObjKey.isObject())
		{
			traceW(L"FileName=%s, CreateOptions=%u, GrantedAccess=%u, PFileContext=%p, FileInfo=%p", FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);
		}

		FileContext->UParam = ctx;
	}

	// �S�~����Ώۂɓo�^
	mResourceSweeper.add(FileContext);

	*PFileContext = FileContext;
	*FileInfo = fileInfo;

	fn.release();
	fc.release();

	return STATUS_SUCCESS;
}

VOID CSDriver::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
	StatsIncr(DoCleanup);
	APP_ASSERT(FileContext);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		if (Flags & FspCleanupDelete)
		{
			NEW_LOG_BLOCK();

			traceW(L"FileName=%s, Flags=%lu", FileName, Flags);

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

			// ���Ă��܂��̂�����A�t���O�����Z�b�g

			ctx->mFlags = 0;
		}
	}
}

VOID CSDriver::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	StatsIncr(DoClose);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	CSDeviceContext* ctx = (CSDeviceContext*)FileContext->UParam;
	if (ctx)
	{
		APP_ASSERT(wcscmp(FileContext->FileName, L"\\") != 0);

		if (ctx->isFile())
		{
			traceW(L"FileName=%s, UParam=%p", FileContext->FileName, FileContext->UParam);
		}

		// �N���E�h�E�X�g���[�W�� UParam �����������

		StatsIncr(_CallClose);
		mCSDevice->close(START_CALLER ctx);

		{
			// �V�K�쐬���ɍ쐬�����ꎞ�����������

			std::lock_guard lock_(CreateNew.mGuard);

			const auto it = CreateNew.mFileInfos.find(FileContext->FileName);
			if (it != CreateNew.mFileInfos.cend())
			{
				traceW(L"erase CreateNew=%s", FileContext->FileName);
				CreateNew.mFileInfos.erase(it);
			}
		}

		delete ctx;
	}

	free(FileContext->FileName);

	FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);

	// �S�~����Ώۂ���폜
	mResourceSweeper.remove(FileContext);

	free(FileContext);
}

// EOF