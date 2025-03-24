#include "WinCseLib.h"
#include "WinCse.hpp"
#include <sstream>
#include <filesystem>

using namespace WinCseLib;


struct ListObjectsTask : public ITask
{
	CanIgnoreDuplicates getCanIgnoreDuplicates() const noexcept override { return CanIgnoreDuplicates::Yes; }
	Priority getPriority() const noexcept override { return Priority::Low; }

	ICSDevice* mCSDevice;
	const ObjectKey mObjectKey;

	ListObjectsTask(ICSDevice* arg, const ObjectKey& argObjKey) :
		mCSDevice(arg), mObjectKey(argObjKey) { }

	std::wstring synonymString() const noexcept override
	{
		std::wstringstream ss;
		ss << L"ListObjectsTask; ";
		ss << mObjectKey.bucket();
		ss << "; ";
		ss << mObjectKey.key();
		
		return ss.str();
	}

	void run(CALLER_ARG0) override
	{
		NEW_LOG_BLOCK();

		traceW(L"Request ListObjects");

		mCSDevice->listObjects(CONT_CALLER mObjectKey, nullptr);
	}
};

NTSTATUS WinCse::DoGetSecurityByName(
	const wchar_t* FileName, PUINT32 PFileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	StatsIncr(DoGetSecurityByName);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');

	traceW(L"FileName: \"%s\"", FileName);

	if (isFileNameIgnored(FileName))
	{
		// "desktop.ini" �Ȃǂ͖���������

		traceW(L"ignore pattern");
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	bool isDir = false;
	bool isFile = false;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�͎Q�Ɨp�f�B���N�g���̏����

		isDir = true;
		traceW(L"detect directory(1)");
	}
	else
	{
		// ������ʉ߂���Ƃ��� FileName �� "\bucket\key" �̂悤�ɂȂ�͂�

		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (!objKey.valid())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);
			return STATUS_OBJECT_NAME_INVALID;
		}

		if (objKey.hasKey())
		{
			// "\bucket\dir" �̃p�^�[��

			if (mCSDevice->headObject(START_CALLER objKey.toDir(), nullptr))
			{
				// �f�B���N�g�����̗p

				isDir = true;
				traceW(L"detect directory(2)");

				// �f�B���N�g�����̃I�u�W�F�N�g���ǂ݂��A�L���b�V�����쐬���Ă���
				// �D��x�͒Ⴍ�A�����ł���

				getWorker(L"delayed")->addTask
				(
					START_CALLER
					new ListObjectsTask{ mCSDevice, objKey.toDir() }
				);
			}
			else
			{
				// "\bucket\dir\file.txt" �̃p�^�[��

				if (mCSDevice->headObject(START_CALLER objKey, nullptr))
				{
					// �t�@�C�����̗p

					isFile = true;
					traceW(L"detect file");
				}
			}
		}
		else // !objKey.HasKey
		{
			// "\bucket" �̃p�^�[��

			if (mCSDevice->headBucket(START_CALLER objKey.bucket()))
			{
				// �f�B���N�g�����̗p

				isDir = true;
				traceW(L"detect directory(3)");

				// �f�B���N�g�����̃I�u�W�F�N�g���ǂ݂��A�L���b�V�����쐬���Ă���
				// �D��x�͒Ⴍ�A�����ł���

				getWorker(L"delayed")->addTask
				(
					START_CALLER
					new ListObjectsTask
					{
						mCSDevice,
						ObjectKey{ objKey.bucket(), L"" }
					}
				);
			}
		}
	}

	if (!isDir && !isFile)
	{
		traceW(L"not found");
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	const HANDLE handle = isFile ? mRefFile.handle() : mRefDir.handle();

	return HandleToInfo(START_CALLER handle, PFileAttributes, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoGetFileInfo);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && FileInfo);

	traceW(L"FileName: \"%s\"", FileContext->FileName);

	return FileNameToFileInfo(START_CALLER FileContext->FileName, FileInfo);
}

NTSTATUS WinCse::DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	StatsIncr(DoGetSecurity);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && SecurityDescriptor && PSecurityDescriptorSize);

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);

	const bool isFile = !FA_IS_DIR(FileContext->FileInfo.FileAttributes);
	traceW(L"isFile: %s", BOOL_CSTRW(isFile));

	const HANDLE handle = isFile ? mRefFile.handle() : mRefDir.handle();

	return HandleToInfo(START_CALLER handle, nullptr, SecurityDescriptor, PSecurityDescriptorSize);
}

void WinCse::DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo)
{
	StatsIncr(DoGetVolumeInfo);

	NEW_LOG_BLOCK();
	APP_ASSERT(Path);
	APP_ASSERT(VolumeInfo);

	traceW(L"Path: %s", Path);
	traceW(L"FreeSize: %llu", VolumeInfo->FreeSize);
	traceW(L"TotalSize: %llu", VolumeInfo->TotalSize);
}

NTSTATUS WinCse::DoRead(PTFS_FILE_CONTEXT* FileContext,
	PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
	StatsIncr(DoRead);
	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred);
	APP_ASSERT(!FA_IS_DIR(FileContext->FileInfo.FileAttributes));		// �t�@�C���̂�

	traceW(L"FileName: \"%s\"", FileContext->FileName);
	traceW(L"FileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Size=%llu Offset=%llu", FileContext->FileInfo.FileSize, Offset);

	//APP_ASSERT(Offset <= FileContext->FileInfo.FileSize);

	// �t�@�C�����쐬����K�v������̂ŁA�T�C�Y==0 �ł� readObject �͌Ăяo��

	return mCSDevice->readObject(START_CALLER (CSDeviceContext*)FileContext->UParam,
		Buffer, Offset, Length, PBytesTransferred);
}

NTSTATUS WinCse::DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern,
	PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
	StatsIncr(DoReadDirectory);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext && Buffer && PBytesTransferred);

	traceW(L"FileName: \"%s\"", FileContext->FileName);

	APP_ASSERT(FA_IS_DIR(FileContext->FileInfo.FileAttributes));

	std::wregex re;
	std::wregex* pRe = nullptr;

	if (Pattern)
	{
		const auto pattern = WildcardToRegexW(Pattern);
		re = std::wregex(pattern);
		pRe = &re;
	}

	// �f�B���N�g���̒��̈ꗗ�擾

	PCWSTR FileName = FileContext->FileName;

	DirInfoListType dirInfoList;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�̓o�P�b�g�ꗗ���

		if (!mCSDevice->listBuckets(START_CALLER &dirInfoList, {}))
		{
			traceW(L"not fouund/1");

			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

		APP_ASSERT(!dirInfoList.empty());
		traceW(L"bucket count: %zu", dirInfoList.size());
	}
	else
	{
		// "\bucket" �܂��� "\bucket\key"

		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (!objKey.valid())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);

			return STATUS_OBJECT_NAME_INVALID;
		}

		// �L�[����̏ꍇ)		bucket & ""     �Ō���
		// �L�[����łȂ��ꍇ)	bucket & "key/" �Ō���

		if (!mCSDevice->listObjects(START_CALLER objKey.toDir(), &dirInfoList))
		{
			traceW(L"not found/2");

			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

		APP_ASSERT(!dirInfoList.empty());
		traceW(L"object count: %zu", dirInfoList.size());
	}

	if (!dirInfoList.empty())
	{
		// �擾�������̂� WinFsp �ɓ]������

		NTSTATUS DirBufferResult = STATUS_SUCCESS;

		if (FspFileSystemAcquireDirectoryBuffer(&FileContext->DirBuffer, 0 == Marker, &DirBufferResult))
		{
			for (const auto& dirInfo: dirInfoList)
			{
				if (pRe)
				{
					if (!std::regex_match(dirInfo->FileNameBuf, *pRe))
					{
						continue;
					}
				}

				if (!FspFileSystemFillDirectoryBuffer(&FileContext->DirBuffer, dirInfo.get(), &DirBufferResult))
				{
					break;
				}
			}

			FspFileSystemReleaseDirectoryBuffer(&FileContext->DirBuffer);
		}

		if (!NT_SUCCESS(DirBufferResult))
		{
			return DirBufferResult;
		}

		FspFileSystemReadDirectoryBuffer(&FileContext->DirBuffer,
			Marker, Buffer, BufferLength, PBytesTransferred);
	}

	return STATUS_SUCCESS;
}

// EOF
