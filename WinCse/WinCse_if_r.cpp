#include "WinCseLib.h"
#include "WinCse.hpp"
#include <cinttypes>
#include <sstream>
#include <filesystem>
#include <mutex>

using namespace WinCseLib;

#undef traceA


struct ListObjectsTask : public ITask
{
	ICloudStorage* mStorage;
	const std::wstring bucket;
	const std::wstring key;

	ListObjectsTask(ICloudStorage* arg, const std::wstring& argBucket, const std::wstring& argKey) :
		mStorage(arg), bucket(argBucket), key(argKey) { }

	std::wstring synonymString()
	{
		std::wstringstream ss;
		ss << L"ListObjectsTask; ";
		ss << bucket;
		ss << "; ";
		ss << key;
		
		return ss.str();
	}

	void run(CALLER_ARG IWorker* worker, const int indent) override
	{
		GetLogger()->traceW_impl(indent, __FUNCTIONW__, __LINE__, __FUNCTIONW__, L"Request ListObjects");

		mStorage->listObjects(CONT_CALLER bucket, key, nullptr, 0, true);
	}
};


#if 0
// ����I�ȏ󋵂ł��������Ȃ��̂Œ���
static std::mutex gGuard;

#define THREAD_SAFE_4DEBUG() \
	std::lock_guard<std::mutex> lock_(gGuard); \
    traceW(L"!!! *** DANGER *** !!! THREAD_SAFE_4DEBUG() ENABLE")

#else
#define THREAD_SAFE_4DEBUG()

#endif


NTSTATUS WinCse::DoGetSecurityByName(
	const wchar_t* FileName, PUINT32 PFileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');

	traceW(L"FileName: %s", FileName);

	if (isIgnoreFileName(FileName))
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
		traceW(L"detect directory/1");
	}
	else
	{
		// ������ʉ߂���Ƃ��� FileName �� "\bucket\key" �̂悤�ɂȂ�͂�

		const BucketKey bk{ FileName };
		if (!bk.OK)
		{
			traceW(L"illegal FileName: %s", FileName);
			return STATUS_INVALID_PARAMETER;
		}

		if (bk.HasKey)
		{
			// "\bucket\key" �̃p�^�[��

			// "key/" �ňꌏ�̂ݎ擾���āA���݂�����f�B���N�g�������݂���Ɣ��肵
			// ���̏����f�B���N�g�������Ƃ��č̗p

			std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

			if (mStorage->listObjects(INIT_CALLER bk.bucket, bk.key + L'/', &dirInfoList, 1, false))
			{
				APP_ASSERT(!dirInfoList.empty());
				APP_ASSERT(dirInfoList.size() == 1);

				// �f�B���N�g�����̗p
				isDir = true;
				traceW(L"detect directory/2");

				// �f�B���N�g�����̃I�u�W�F�N�g���ǂ݂��A�L���b�V�����쐬���Ă���
				// �D��x�͒Ⴍ�A�����ł���
				mDelayedWorker->addTask(new ListObjectsTask{ mStorage, bk.bucket, bk.key + L'/' }, CanIgnore::YES, Priority::LOW);
			}

			if (!isDir)
			{
				// �t�@�C�����̊��S��v�Ō���

				if (mStorage->headObject(INIT_CALLER bk.bucket, bk.key, nullptr))
				{
					// �t�@�C�����̗p
					isFile = true;
					traceW(L"detect file");
				}
			}
		}
		else // !bk.HasKey
		{
			// "\bucket" �̃p�^�[��

			if (mStorage->headBucket(INIT_CALLER bk.bucket))
			{
				// �f�B���N�g�����̗p
				isDir = true;
				traceW(L"detect directory/3");

				// �f�B���N�g�����̃I�u�W�F�N�g���ǂ݂��A�L���b�V�����쐬���Ă���
				// �D��x�͒Ⴍ�A�����ł���
				mDelayedWorker->addTask(new ListObjectsTask{ mStorage, bk.bucket, L"" }, CanIgnore::YES, Priority::LOW);
			}
		}
	}

	if (!isDir && !isFile)
	{
		traceW(L"not found");
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	const HANDLE handle = isFile ? mFileRefHandle : mDirRefHandle;

#ifdef _DEBUG
	std::wstring path;
	HandleToPath(handle, path);
	traceW(L"selected path is %s", path.c_str());

	std::wstring sdstr;
	PathToSDStr(path, sdstr);
	traceW(L"sdstr is %s", sdstr.c_str());
#endif

	return HandleToInfo(handle, PFileAttributes, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoOpen(const wchar_t* FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
	PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!isIgnoreFileName(FileName));
	APP_ASSERT(PFileContext);
	APP_ASSERT(FileInfo);

	traceW(L"FileName: %s", FileName);

	PTFS_FILE_CONTEXT* FileContext = nullptr;
	FSP_FSCTL_FILE_INFO fileInfo = {};
	NTSTATUS Result = STATUS_UNSUCCESSFUL;

	Result = FileNameToFileInfo(FileName, &fileInfo);
	if (!NT_SUCCESS(Result))
	{
		traceW(L"fault: FileNameToFileInfo");
		goto exit;
	}

	// �O�̂��ߌ���
	APP_ASSERT(fileInfo.FileAttributes);
	APP_ASSERT(fileInfo.CreationTime);

	// WinFsp �ɕۑ������t�@�C���E�R���e�L�X�g�𐶐�
	// ���̃������� WinFsp �� Close() �ō폜����邽�߉���s�v

	FileContext = (PTFS_FILE_CONTEXT*)calloc(1, sizeof *FileContext);
	if (!FileContext)
	{
		traceW(L"fault: allocate FileContext");
		Result = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	FileContext->Open.FileName = _wcsdup(FileName);
	if (!FileContext->Open.FileName)
	{
		traceW(L"fault: allocate FileContext->OpenFileName");
		Result = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	if (wcscmp(FileName, L"\\") == 0)
	{
		traceW(L"root access");

		APP_ASSERT(fileInfo.FileSize == 0);
	}
	else
	{
		const BucketKey bk{ FileName };

		if (!bk.OK)
		{
			traceW(L"illegal FileName: %s", FileName);
			return STATUS_INVALID_PARAMETER;
		}

		if (fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// �f�B���N�g���ւ̃A�N�Z�X

			APP_ASSERT(fileInfo.FileSize == 0);
		}
		else
		{
			// �t�@�C���ւ̃A�N�Z�X

			APP_ASSERT(bk.HasKey);

			// �}���`�p�[�g��������ōő�t�@�C���E�T�C�Y�̐������Ȃ���

			traceW(L"FileSize: %" PRIu64, fileInfo.FileSize);

			if (mMaxFileSize > 0)
			{
				if (fileInfo.FileSize > 1024ULL * 1024 * mMaxFileSize)
				{
					Result = STATUS_DEVICE_NOT_READY;
					traceW(L"%" PRIu64 ": When a file size exceeds the maximum size that can be opened.", fileInfo.FileSize);
					goto exit;
				}
			}

			// �N���E�h�E�X�g���[�W�̃R���e�L�X�g�� CSData �ɕۑ�������

			if (!mStorage->openFile(INIT_CALLER
				bk.bucket, bk.key, CreateOptions, GrantedAccess, fileInfo, &FileContext->Open.CSData))
			{
				traceW(L"fault: openFile");
				Result = STATUS_DEVICE_NOT_READY;
				goto exit;
			}
		}
	}

	FileContext->Open.FileInfo = fileInfo;

	// SUCSESS RETURN

	*PFileContext = FileContext;
	FileContext = nullptr;

	*FileInfo = fileInfo;

exit:
	if (FileContext)
	{
		free(FileContext->Open.FileName);
	}
	free(FileContext);

	traceW(L"return %ld", Result);

	return Result;
}

NTSTATUS WinCse::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);

	traceW(L"Open.FileName: %s", FileContext->Open.FileName);

	if (FileContext->Open.CSData)
	{
		// �N���E�h�E�X�g���[�W�� CSData �����������

		mStorage->closeFile(INIT_CALLER FileContext->Open.CSData);
	}

	free(FileContext->Open.FileName);

	// FileContext �͌Ăяo������ free ���Ă���

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);
	APP_ASSERT(FileInfo);

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);
	PCWSTR FileName = FileContext->Open.FileName;

	FSP_FSCTL_FILE_INFO fileInfo = {};

	NTSTATUS Result = FileNameToFileInfo(FileName, &fileInfo);
	if (!NT_SUCCESS(Result))
	{
		traceW(L"fault: FileNameToFileInfo");
		goto exit;
	}

	*FileInfo = fileInfo;

	Result = STATUS_SUCCESS;

exit:

	return Result;
}

NTSTATUS WinCse::DoGetSecurity(PTFS_FILE_CONTEXT* FileContext,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);
	traceW(L"OpenFileAttributes: %u", FileContext->Open.FileInfo.FileAttributes);

	const bool isFile = !(FileContext->Open.FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

	traceW(L"isFile: %s", isFile ? L"true" : L"false");

	const HANDLE handle = isFile ? mFileRefHandle : mDirRefHandle;

	return HandleToInfo(handle, nullptr, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(Path);
	APP_ASSERT(VolumeInfo);

	traceW(L"Path: %s", Path);
	traceW(L"FreeSize: %" PRIu64, VolumeInfo->FreeSize);
	traceW(L"TotalSize: %" PRIu64, VolumeInfo->TotalSize);

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoRead(PTFS_FILE_CONTEXT* FileContext,
	PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);
	APP_ASSERT(Buffer);
	APP_ASSERT(PBytesTransferred);
	APP_ASSERT(!(FileContext->Open.FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY));

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);
	traceW(L"OpenFileAttributes: %u", FileContext->Open.FileInfo.FileAttributes);

	bool ret = mStorage->readFile(INIT_CALLER FileContext->Open.CSData,
		Buffer, Offset, Length, PBytesTransferred);

	traceW(L"readFile return %s", ret ? L"true" : L"false");

	if (!ret)
	{
		return STATUS_DEVICE_DATA_ERROR;
	}

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern,
	PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
	NEW_LOG_BLOCK();
	THREAD_SAFE_4DEBUG();
	APP_ASSERT(FileContext);

	traceW(L"OpenFileName: %s", FileContext->Open.FileName);

	APP_ASSERT(FileContext->Open.FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

	std::wregex re;
	std::wregex* pRe = nullptr;

	if (Pattern)
	{
		const auto pattern = WildcardToRegexW(Pattern);
		re = std::wregex(pattern);
		pRe = &re;
	}

	// �f�B���N�g���̒��̈ꗗ�擾

	PCWSTR FileName = FileContext->Open.FileName;

	std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�̓o�P�b�g�ꗗ���

		if (!mStorage->listBuckets(INIT_CALLER &dirInfoList, {}))
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

		const BucketKey bk{ FileName };
		if (!bk.OK)
		{
			traceW(L"illegal FileName: %s", FileName);

			return STATUS_INVALID_PARAMETER;
		}

		// �L�[����̏ꍇ)		bucket & ""     �Ō���
		// �L�[����łȂ��ꍇ)	bucket & "key/" �Ō���

		const auto key = bk.HasKey ? bk.key + L'/' : bk.key;

		if (!mStorage->listObjects(INIT_CALLER bk.bucket, key, &dirInfoList, 0, true))
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
