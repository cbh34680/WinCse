#include "WinCseLib.h"
#include "CSDriver.hpp"

using namespace WCSE;


NTSTATUS CSDriver::getFileInfoByFileName(CALLER_ARG PCWSTR fileName,
	FSP_FSCTL_FILE_INFO* pFileInfo, FileNameType* pFileNameType)
{
	APP_ASSERT(pFileInfo);

	if (wcscmp(fileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�͎Q�Ɨp�f�B���N�g���̏����

		*pFileNameType = FileNameType::RootDirectory;

		return GetFileInfoInternal(this->mRefDir.handle(), pFileInfo);
	}
	else
	{
		ObjectKey objKey{ ObjectKey::fromWinPath(fileName) };
		if (objKey.invalid())
		{
			return STATUS_OBJECT_NAME_INVALID;
		}

		if (objKey.isBucket())
		{
			// "\bucket" �̃p�^�[��

			APP_ASSERT(objKey.isBucket());

			const auto dirInfo{ mCSDevice->headBucket(CONT_CALLER objKey.bucket()) };
			if (dirInfo)
			{
				*pFileInfo = dirInfo->FileInfo;
				*pFileNameType = FileNameType::Bucket;

				return STATUS_SUCCESS;
			}
		}
		else if (objKey.isObject())
		{
			// "\bucket\***" �̃p�^�[��

			{
				// �V�K�쐬���͂܂��X�g���[�W�ɑ��݂��Ȃ���ԂȂ̂ŁA����������ɂ��
				// �₢���킹�ɉ񓚂���

				std::lock_guard lock_(CreateNew.mGuard);

				const auto it = CreateNew.mFileInfos.find(fileName);
				if (it != CreateNew.mFileInfos.end())
				{
					NEW_LOG_BLOCK();

					*pFileInfo = it->second;

					if (FA_IS_DIRECTORY(it->second.FileAttributes))
					{
						traceW(L"found: CreateNew=%s [DIRECTORY]", fileName);

						*pFileNameType = FileNameType::DirectoryObject;
					}
					else
					{
						traceW(L"found: CreateNew=%s [FILE]", fileName);

						*pFileNameType = FileNameType::FileObject;
					}

					return STATUS_SUCCESS;
				}
			}

			// �������O�̃t�@�C���ƃf�B���N�g�������݂����Ƃ��ɁA�f�B���N�g����D�悷�邽��
			// �����̖��O���f�B���N�g���ɕϊ����X�g���[�W�𒲂ׁA���݂��Ȃ��Ƃ��̓t�@�C���Ƃ��Ē��ׂ�

			auto dirInfo = mCSDevice->headObject(CONT_CALLER objKey.toDir());
			if (dirInfo)
			{
				// "\bucket\dir" �̃p�^�[��
				// 
				// �f�B���N�g�����̗p

				*pFileInfo = dirInfo->FileInfo;
				*pFileNameType = FileNameType::DirectoryObject;

				return STATUS_SUCCESS;
			}

			dirInfo = mCSDevice->headObject(CONT_CALLER objKey);
			if (dirInfo)
			{
				// "\bucket\dir\file.txt" �̃p�^�[��
				// 
				// �t�@�C�����̗p

				*pFileInfo = dirInfo->FileInfo;
				*pFileNameType = FileNameType::FileObject;

				return STATUS_SUCCESS;
			}
		}
		else
		{
			APP_ASSERT(0);
		}
	}

	NEW_LOG_BLOCK();
	traceW(L"not found: fileName=%s", fileName);

	return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
}

NTSTATUS CSDriver::DoGetSecurityByName(PCWSTR FileName, PUINT32 PFileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize)
{
	StatsIncr(DoGetSecurityByName);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');

	if (this->shouldIgnoreFileName(FileName))
	{
		// "desktop.ini" �Ȃǂ͖���������

		//traceW(L"ignore pattern");
		return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
	}

	FSP_FSCTL_FILE_INFO fileInfo;
	FileNameType fileNameType;

	auto ntstatus = this->getFileInfoByFileName(START_CALLER FileName, &fileInfo, &fileNameType);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: getFileInfoByFileName, FileName=%s", FileName);
		return ntstatus;
	}

	HANDLE Handle = INVALID_HANDLE_VALUE;

	switch (fileNameType)
	{
		case FileNameType::DirectoryObject:
		{
			traceW(L"FileName=%s [DIRECTORY]", FileName);

			[[fallthrough]];
		}
		case FileNameType::Bucket:
		case FileNameType::RootDirectory:
		{
			Handle = mRefDir.handle();
			break;
		}

		case FileNameType::FileObject:
		{
			traceW(L"FileName=%s [FILE]", FileName);

			Handle = mRefFile.handle();
			break;
		}

		default:
		{
			APP_ASSERT(0);
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
}

// EOF