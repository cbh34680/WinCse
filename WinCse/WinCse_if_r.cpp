#include "WinCseLib.h"
#include "WinCse.hpp"
#include <sstream>
#include <filesystem>
#include <mutex>

using namespace WinCseLib;

#undef traceA


struct ListObjectsTask : public ITask
{
	ICSDevice* mCSDevice;
	const std::wstring mBucket;
	const std::wstring mKey;

	ListObjectsTask(ICSDevice* arg, const std::wstring& argBucket, const std::wstring& argKey) :
		mCSDevice(arg), mBucket(argBucket), mKey(argKey) { }

	std::wstring synonymString()
	{
		std::wstringstream ss;
		ss << L"ListObjectsTask; ";
		ss << mBucket;
		ss << "; ";
		ss << mKey;
		
		return ss.str();
	}

	void run(CALLER_ARG0) override
	{
		NEW_LOG_BLOCK();

		traceW(L"Request ListObjects");

		mCSDevice->listObjects(CONT_CALLER mBucket, mKey, nullptr);
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

		const BucketKey bk{ FileName };
		if (!bk.OK())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);
			return STATUS_INVALID_PARAMETER;
		}

		if (bk.hasKey())
		{
			// "\bucket\dir" �̃p�^�[��

			if (mCSDevice->headObject(START_CALLER bk.bucket(), bk.key() + L'/', nullptr))
			{
				// �f�B���N�g�����̗p
				isDir = true;
				traceW(L"detect directory(2)");

				// �f�B���N�g�����̃I�u�W�F�N�g���ǂ݂��A�L���b�V�����쐬���Ă���
				// �D��x�͒Ⴍ�A�����ł���
				mDelayedWorker->addTask(START_CALLER new ListObjectsTask{ mCSDevice, bk.bucket(), bk.key() + L'/' }, Priority::Low, CanIgnore::Yes);
			}
			else
			{
				// "\bucket\dir\file.txt" �̃p�^�[��

				if (mCSDevice->headObject(START_CALLER bk.bucket(), bk.key(), nullptr))
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

			if (mCSDevice->headBucket(START_CALLER bk.bucket()))
			{
				// �f�B���N�g�����̗p
				isDir = true;
				traceW(L"detect directory(3)");

				// �f�B���N�g�����̃I�u�W�F�N�g���ǂ݂��A�L���b�V�����쐬���Ă���
				// �D��x�͒Ⴍ�A�����ł���
				mDelayedWorker->addTask(START_CALLER new ListObjectsTask{ mCSDevice, bk.bucket(), L"" }, Priority::Low, CanIgnore::Yes);
			}
		}
	}

	if (!isDir && !isFile)
	{
		traceW(L"not found");
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	const HANDLE handle = isFile ? mFileRefHandle : mDirRefHandle;

#if 0
	std::wstring path;
	HandleToPath(handle, path);
	traceW(L"selected path is %s", path.c_str());

	std::wstring sdstr;
	PathToSDStr(path, sdstr);
	traceW(L"sdstr is %s", sdstr.c_str());
#endif

	return HandleToInfo(START_CALLER handle, PFileAttributes, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
	StatsIncr(DoGetFileInfo);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);
	APP_ASSERT(FileInfo);

	traceW(L"OpenFileName: \"%s\"", FileContext->FileName);
	PCWSTR FileName = FileContext->FileName;

	FSP_FSCTL_FILE_INFO fileInfo = {};

	NTSTATUS Result = FileNameToFileInfo(START_CALLER FileName, &fileInfo);
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
	StatsIncr(DoGetSecurity);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"OpenFileName: \"%s\"", FileContext->FileName);
	traceW(L"OpenFileAttributes: %u", FileContext->FileInfo.FileAttributes);

	const bool isFile = !(FileContext->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

	traceW(L"isFile: %s", isFile ? L"true" : L"false");

	const HANDLE handle = isFile ? mFileRefHandle : mDirRefHandle;

	return HandleToInfo(START_CALLER handle, nullptr, SecurityDescriptor, PSecurityDescriptorSize);
}

NTSTATUS WinCse::DoGetVolumeInfo(PCWSTR Path, FSP_FSCTL_VOLUME_INFO* VolumeInfo)
{
	StatsIncr(DoGetVolumeInfo);

	NEW_LOG_BLOCK();
	APP_ASSERT(Path);
	APP_ASSERT(VolumeInfo);

	traceW(L"Path: %s", Path);
	traceW(L"FreeSize: %llu", VolumeInfo->FreeSize);
	traceW(L"TotalSize: %llu", VolumeInfo->TotalSize);

	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS WinCse::DoRead(PTFS_FILE_CONTEXT* FileContext,
	PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
	StatsIncr(DoRead);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);
	APP_ASSERT(Buffer);
	APP_ASSERT(PBytesTransferred);
	APP_ASSERT(!(FileContext->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY));
	APP_ASSERT(Offset <= FileContext->FileInfo.FileSize)

	traceW(L"OpenFileName: \"%s\"", FileContext->FileName);
	traceW(L"OpenFileAttributes: %u", FileContext->FileInfo.FileAttributes);
	traceW(L"Size=%llu Offset=%llu", FileContext->FileInfo.FileSize, Offset);

	if (Offset == FileContext->FileInfo.FileSize)
	{
		// �t�@�C���̍Ō�܂œ��B���Ă���̂ŁA����ȏ�͓ǂޕK�v���Ȃ�

		traceW(L"EOF marker has been reached");
		return STATUS_END_OF_FILE;
	}

	bool ret = mCSDevice->readFile(START_CALLER FileContext->UParam,
		Buffer, Offset, Length, PBytesTransferred);

	traceW(L"readFile return %s", ret ? L"true" : L"false");

	return ret ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
}

NTSTATUS WinCse::DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern,
	PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
	StatsIncr(DoReadDirectory);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileContext);

	traceW(L"OpenFileName: \"%s\"", FileContext->FileName);

	APP_ASSERT(FileContext->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);

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

		const BucketKey bk{ FileName };
		if (!bk.OK())
		{
			traceW(L"illegal FileName: \"%s\"", FileName);

			return STATUS_INVALID_PARAMETER;
		}

		// �L�[����̏ꍇ)		bucket & ""     �Ō���
		// �L�[����łȂ��ꍇ)	bucket & "key/" �Ō���

		const auto key{ bk.hasKey() ? bk.key() + L'/' : bk.key() };

		if (!mCSDevice->listObjects(START_CALLER bk.bucket(), key, &dirInfoList))
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
