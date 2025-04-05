#include "WinCseLib.h"
#include "CSDriver.hpp"
#include <sstream>

using namespace WCSE;


NTSTATUS CSDriver::getFileInfoByName(CALLER_ARG PCWSTR fileName, FSP_FSCTL_FILE_INFO* pFileInfo, FileNameType* pType /* nullable */)
{
	APP_ASSERT(pFileInfo);

	if (wcscmp(fileName, L"\\") == 0)
	{
		// "\" へのアクセスは参照用ディレクトリの情報を提供

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
			if (mCSDevice->headObject_Dir(CONT_CALLER objKey.toDir(), pFileInfo))
			{
				// "\bucket\dir" のパターン
				// 
				// ディレクトリを採用

				if (pType)
				{
					*pType = FileNameType::DirectoryObject;
				}

				return STATUS_SUCCESS;
			}
			else if (mCSDevice->headObject_File(CONT_CALLER objKey, pFileInfo))
			{
				// "\bucket\dir\file.txt" のパターン
				// 
				// ファイルを採用

				if (pType)
				{
					*pType = FileNameType::FileObject;
				}

				return STATUS_SUCCESS;
			}
		}
		else
		{
			// "\bucket" のパターン

			if (mCSDevice->headBucket(CONT_CALLER objKey.bucket(), pFileInfo))
			{
				if (pType)
				{
					*pType = FileNameType::Bucket;
				}

				return STATUS_SUCCESS;
			}
		}
	}

	NEW_LOG_BLOCK();
	traceW(L"not found: fileName=%s", fileName);

	return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
}

NTSTATUS CSDriver::FileNameToFileInfo(CALLER_ARG PCWSTR FileName, FSP_FSCTL_FILE_INFO* pFileInfo)
{
	APP_ASSERT(FileName);
	APP_ASSERT(pFileInfo);
	APP_ASSERT(FileName[0] == L'\\');
	APP_ASSERT(!shouldIgnoreFileName(FileName))

	return getFileInfoByName(CONT_CALLER FileName, pFileInfo, nullptr);
}

NTSTATUS CSDriver::DoGetSecurityByName(
	PCWSTR FileName, PUINT32 PFileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize)
{
	StatsIncr(DoGetSecurityByName);

	NEW_LOG_BLOCK();
	APP_ASSERT(FileName);
	APP_ASSERT(FileName[0] == L'\\');

	if (shouldIgnoreFileName(FileName))
	{
		// "desktop.ini" などは無視させる

		//traceW(L"ignore pattern");
		return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
	}

	FSP_FSCTL_FILE_INFO fileInfo;
	FileNameType fileNameType;

	NTSTATUS ntstatus = getFileInfoByName(START_CALLER FileName, &fileInfo, &fileNameType);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: getFileInfoByName, FileName=%s", FileName);
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