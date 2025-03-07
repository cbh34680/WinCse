#include "WinCseLib.h"
#include "WinCse.hpp"
#include <sstream>


using namespace WinCseLib;


bool WinCse::isFileNameIgnored(const wchar_t* arg)
{
	// desktop.ini �Ȃǃ��N�G�X�g�������߂�����͖̂�������

	if (mIgnoredFileNamePatterns.mark_count() == 0)
	{
		// ���K�\�����ݒ肳��Ă��Ȃ�
		return false;
	}

	return std::regex_match(std::wstring(arg), mIgnoredFileNamePatterns);
}

//
// passthrough.c ����q��
//
NTSTATUS WinCse::HandleToInfo(CALLER_ARG HANDLE hFile, PUINT32 PFileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	NEW_LOG_BLOCK();

	FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo = {};
	DWORD SecurityDescriptorSizeNeeded = 0;

	if (0 != PFileAttributes)
	{
		if (!::GetFileInformationByHandleEx(hFile,
			FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
		{
			return FspNtStatusFromWin32(::GetLastError());
		}

		traceW(L"FileAttributes: %u", AttributeTagInfo.FileAttributes);
		traceW(L"\tdetect: %s", AttributeTagInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY ? L"dir" : L"file");

		*PFileAttributes = AttributeTagInfo.FileAttributes;
	}

	if (0 != PSecurityDescriptorSize)
	{
		if (!::GetKernelObjectSecurity(hFile,
			OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			SecurityDescriptor, (DWORD)*PSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
		{
			*PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
			return FspNtStatusFromWin32(::GetLastError());
		}

		traceW(L"SecurityDescriptorSizeNeeded: %u", SecurityDescriptorSizeNeeded);

		*PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
	}

	return STATUS_SUCCESS;
}

NTSTATUS WinCse::FileNameToFileInfo(CALLER_ARG const wchar_t* FileName, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(pFileInfo);

	FSP_FSCTL_FILE_INFO fileInfo = {};

	bool isDir = false;
	bool isFile = false;

	if (wcscmp(FileName, L"\\") == 0)
	{
		// "\" �ւ̃A�N�Z�X�͎Q�Ɨp�f�B���N�g���̏����
		isDir = true;
		traceW(L"detect directory(1)");

		GetFileInfoInternal(this->mDirRefHandle, &fileInfo);
	}
	else
	{
		// �����ɗ���̂� "\\bucket" ���� "\\bucket\\key" �̂�

		// DoGetSecurityByName() �Ɠ��l�̌��������āA���̌��ʂ� PFileContext
		// �� FileInfo �ɔ��f������

		const BucketKey bk{ FileName };
		if (!bk.OK())
		{
			traceW(L"illegal FileName: %s", FileName);

			return STATUS_INVALID_PARAMETER;
		}

		if (bk.hasKey())
		{
			// "\bucket\dir" �̃p�^�[��

			if (mCSDevice->headObject(CONT_CALLER bk.bucket(), bk.key() + L'/', &fileInfo))
			{
				// �f�B���N�g�����̗p

				isDir= true;
				traceW(L"detect directory(2)");
			}
			else
			{
				// "\bucket\dir\file.txt" �̃p�^�[��

				if (mCSDevice->headObject(CONT_CALLER bk.bucket(), bk.key(), &fileInfo))
				{
					// �t�@�C�����̗p

					isFile = true;
					traceW(L"detect file");
				}
			}
		}
		else
		{
			// "\bucket" �̃p�^�[��

			// HeadBucket �ł̓��^��񂪎擾�ł��Ȃ��̂� ListBuckets ���疼�O����v������̂��擾

			DirInfoListType dirInfoList;

			// ���O���w�肵�ă��X�g���擾

			if (mCSDevice->listBuckets(CONT_CALLER &dirInfoList, { bk.bucket() }))
			{
				APP_ASSERT(dirInfoList.size() == 1);

				// �f�B���N�g�����̗p

				isDir = true;
				traceW(L"detect directory(3)");

				fileInfo = (*dirInfoList.begin())->FileInfo;
			}
		}
	}

	if (!isDir && !isFile)
	{
		traceW(L"not found");

		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	*pFileInfo = fileInfo;

	return STATUS_SUCCESS;
}

//
// BucketKey
//

// ��������o�P�b�g���ƃL�[�ɕ���
BucketKey::BucketKey(const wchar_t* arg)
{
	std::wstringstream input{ arg };

	std::vector<std::wstring> tokens;
	std::wstring token;

	while (std::getline(input, token, L'\\'))
	{
		tokens.push_back(token);
	}

	switch (tokens.size())
	{
		case 0:
		case 1:
		{
			return;
		}
		case 2:
		{
			mBucket = std::move(tokens[1]);
			break;
		}
		default:
		{
			mBucket = std::move(tokens[1]);

			std::wstringstream output;
			for (int i = 2; i < tokens.size(); ++i)
			{
				if (i != 2)
				{
					output << L'/';
				}
				output << std::move(tokens[i]);
			}

			mKey = output.str();
			mHasKey = true;

			break;
		}
	}

	mOK = true;
}

// EOF