#include "WinCseLib.h"
#include "WinCse.hpp"
#include <sstream>

using namespace WinCseLib;


NTSTATUS WinCse::getFileInfoByName(CALLER_ARG const wchar_t* fileName, FSP_FSCTL_FILE_INFO* pFileInfo, FileNameType* pType /* nullable */, ObjectKey* pObjKey)
{
	APP_ASSERT(pFileInfo);

	if (wcscmp(fileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�͎Q�Ɨp�f�B���N�g���̏����

		if (pType)
		{
			*pType = FileNameType::RootDirectory;
		}

		return GetFileInfoInternal(this->mRefDir.handle(), pFileInfo);
	}
	else
	{
		ObjectKey objKey{ ObjectKey::fromWinPath(fileName) };
		if (objKey.invalid())
		{
			return STATUS_OBJECT_NAME_INVALID;
		}

		if (objKey.hasKey())
		{
			if (mCSDevice->headObject(CONT_CALLER objKey.toDir(), pFileInfo))
			{
				// "\bucket\dir" �̃p�^�[��
				// 
				// �f�B���N�g�����̗p

				if (pType)
				{
					*pType = FileNameType::DirectoryObject;
				}

				if (pObjKey)
				{
					*pObjKey = objKey.toDir();
				}

				return STATUS_SUCCESS;
			}
			else if (mCSDevice->headObject(CONT_CALLER objKey, pFileInfo))
			{
				// "\bucket\dir\file.txt" �̃p�^�[��
				// 
				// �t�@�C�����̗p

				if (pType)
				{
					*pType = FileNameType::FileObject;
				}

				if (pObjKey)
				{
					*pObjKey = std::move(objKey);
				}

				return STATUS_SUCCESS;
			}
		}
		else
		{
			// "\bucket" �̃p�^�[��

#if 0
			// HeadBucket �ł̓��^��񂪎擾�ł��Ȃ��̂� ListBuckets ���疼�O����v������̂��擾

			DirInfoListType dirInfoList;

			// ���O���w�肵�ă��X�g���擾

			if (mCSDevice->listBuckets(CONT_CALLER &dirInfoList, { objKey.bucket() }))
			{
				APP_ASSERT(dirInfoList.size() == 1);

				// �f�B���N�g�����̗p

				*pFileInfo = (*dirInfoList.begin())->FileInfo;

				if (pType)
				{
					*pType = FileNameType::Bucket;
				}

				if (pObjKey)
				{
					*pObjKey = std::move(objKey);
				}

				return STATUS_SUCCESS;
			}

#else
			const DirInfoType dirInfo = mCSDevice->getBucket(CONT_CALLER objKey.bucket());
			if (dirInfo)
			{
				*pFileInfo = dirInfo->FileInfo;

				if (pType)
				{
					*pType = FileNameType::Bucket;
				}

				if (pObjKey)
				{
					*pObjKey = std::move(objKey);
				}

				return STATUS_SUCCESS;
			}

#endif
		}
	}

	return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
}


NTSTATUS WinCse::FileNameToFileInfo(CALLER_ARG const wchar_t* FileName, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(pFileInfo);
	APP_ASSERT(FileName[0] == L'\\');

	traceW(L"FileName: \"%s\"", FileName);

	APP_ASSERT(!isFileNameIgnored(FileName))

#if 0
	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�͎Q�Ɨp�f�B���N�g���̏����

		traceW(L"detect directory(1)");

		return GetFileInfoInternal(this->mRefDir.handle(), pFileInfo);
	}

	// �����ɗ���̂� "\\bucket" ���� "\\bucket\\key" �̂�

	// DoGetSecurityByName() �Ɠ��l�̌��������āA���̌��ʂ� FileInfo �ɔ��f������

	const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
	if (objKey.invalid())
	{
		traceW(L"illegal FileName: %s", FileName);

		return STATUS_OBJECT_NAME_INVALID;
	}

	if (objKey.hasKey())
	{
		if (mCSDevice->headObject(CONT_CALLER objKey.toDir(), pFileInfo))
		{
			// "\bucket\dir" �̃p�^�[��
			// 
			// �f�B���N�g�����̗p

			traceW(L"detect directory(2)");
			return STATUS_SUCCESS;
		}
		else if (mCSDevice->headObject(CONT_CALLER objKey, pFileInfo))
		{
			// "\bucket\dir\file.txt" �̃p�^�[��
			// 
			// �t�@�C�����̗p

			traceW(L"detect file");
			return STATUS_SUCCESS;
		}
	}
	else
	{
		// "\bucket" �̃p�^�[��

		// HeadBucket �ł̓��^��񂪎擾�ł��Ȃ��̂� ListBuckets ���疼�O����v������̂��擾

		DirInfoListType dirInfoList;

		// ���O���w�肵�ă��X�g���擾

		if (mCSDevice->listBuckets(CONT_CALLER &dirInfoList, { objKey.bucket() }))
		{
			APP_ASSERT(dirInfoList.size() == 1);

			// �f�B���N�g�����̗p

			traceW(L"detect directory(3)");

			*pFileInfo = (*dirInfoList.begin())->FileInfo;
			return STATUS_SUCCESS;
		}
	}

	traceW(L"not found");

	return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);

#else
	return getFileInfoByName(CONT_CALLER FileName, pFileInfo, nullptr, nullptr);

#endif
}

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
		return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
	}

#if 0
	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�͎Q�Ɨp�f�B���N�g���̏����

		traceW(L"detect directory(1)");

		return HandleToInfo(mRefDir.handle(), PFileAttributes, SecurityDescriptor, PSecurityDescriptorSize);
	}

	// ������ʉ߂���Ƃ��� FileName �� "\bucket\key" �̂悤�ɂȂ�͂�

	HANDLE Handle = INVALID_HANDLE_VALUE;

	const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
	if (!objKey.valid())
	{
		traceW(L"illegal FileName: \"%s\"", FileName);
		return STATUS_OBJECT_NAME_INVALID;
	}

	FSP_FSCTL_FILE_INFO fileInfo{};

	if (objKey.hasKey())
	{
		if (mCSDevice->headObject(START_CALLER objKey.toDir(), &fileInfo))
		{
			// "\bucket\dir" �̃p�^�[��
			// 
			// �f�B���N�g�����̗p

			traceW(L"detect directory(2)");

			// �f�B���N�g�����̃I�u�W�F�N�g���ǂ݂��A�L���b�V�����쐬���Ă���
			// �D��x�͒Ⴍ�A�����ł���

			getWorker(L"delayed")->addTask
			(
				START_CALLER
				new ListObjectsTask{ mCSDevice, objKey.toDir() }
			);

			Handle = mRefDir.handle();
		}
		else if (mCSDevice->headObject(START_CALLER objKey, &fileInfo))
		{
			// "\bucket\dir\file.txt" �̃p�^�[��
			// 
			// �t�@�C�����̗p

			traceW(L"detect file");

			Handle = mRefFile.handle();
		}
	}
	else // !objKey.HasKey
	{
		if (mCSDevice->headBucket(START_CALLER objKey.bucket(), &fileInfo))
		{
			// "\bucket" �̃p�^�[��
			// 
			// �f�B���N�g�����̗p

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

			Handle = mRefDir.handle();
		}
	}

	if (Handle == INVALID_HANDLE_VALUE)
	{
		traceW(L"fault: Handle is invalid");

		return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
	}

	APP_ASSERT(fileInfo.CreationTime);		// �O�̂��߃`�F�b�N

	if (PFileAttributes)
	{
		*PFileAttributes = fileInfo.FileAttributes;
	}

	return HandleToSecurityInfo(Handle, SecurityDescriptor, PSecurityDescriptorSize);

#else
	FSP_FSCTL_FILE_INFO fileInfo;
	FileNameType fileNameType;
	ObjectKey objKey;

	NTSTATUS ntstatus = getFileInfoByName(START_CALLER FileName, &fileInfo, &fileNameType, &objKey);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: getFileInfoByName");
		return ntstatus;
	}

	HANDLE Handle = INVALID_HANDLE_VALUE;

	switch (fileNameType)
	{
		case FileNameType::RootDirectory:
		{
			APP_ASSERT(objKey.invalid());

			Handle = mRefDir.handle();
			break;
		}

		case FileNameType::Bucket:
		{
			APP_ASSERT(objKey.isBucket());

			Handle = mRefDir.handle();
			break;
		}

		case FileNameType::DirectoryObject:
		{
			APP_ASSERT(objKey.meansDir());

			Handle = mRefDir.handle();
			break;
		}

		case FileNameType::FileObject:
		{
			APP_ASSERT(objKey.meansFile());

			Handle = mRefFile.handle();
			break;
		}
	}

	if (PFileAttributes)
	{
		*PFileAttributes = fileInfo.FileAttributes;
	}

	ntstatus = HandleToSecurityInfo(Handle, SecurityDescriptor, PSecurityDescriptorSize);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: HandleToSecurityInfo");
		return ntstatus;
	}

	return STATUS_SUCCESS;
#endif
}

// EOF