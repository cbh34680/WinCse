#include "CSDriver.hpp"

using namespace WCSE;


NTSTATUS CSDriver::getFileInfoByFileName(CALLER_ARG PCWSTR fileName,
	FSP_FSCTL_FILE_INFO* pFileInfo, FileNameType* pFileNameType)
{
	APP_ASSERT(pFileInfo);

	if (wcscmp(fileName, L"\\") == 0)
	{
		// "\" へのアクセスは参照用ディレクトリの情報を提供

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
			// "\bucket" のパターン

			APP_ASSERT(objKey.isBucket());

			DirInfoType dirInfo;

			if (mCSDevice->headBucket(CONT_CALLER objKey.bucket(), &dirInfo))
			{
				*pFileInfo = dirInfo->FileInfo;
				*pFileNameType = FileNameType::Bucket;

				return STATUS_SUCCESS;
			}
		}
		else if (objKey.isObject())
		{
			// "\bucket\***" のパターン

			{
				// 新規作成時はまだストレージに存在しない状態なので、メモリ操作により
				// 問い合わせに回答する

				std::lock_guard lock_(CreateNew.mGuard);

				const auto it = CreateNew.mFileInfos.find(fileName);
				if (it != CreateNew.mFileInfos.cend())
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

			// 同じ名前のファイルとディレクトリが存在したときに、ディレクトリを優先するため
			// 引数の名前をディレクトリに変換しストレージを調べ、存在しないときはファイルとして調べる

			DirInfoType dirInfo;

			if (mCSDevice->headObject(CONT_CALLER objKey.toDir(), &dirInfo))
			{
				// "\bucket\dir" のパターン
				// 
				// ディレクトリを採用

				*pFileInfo = dirInfo->FileInfo;
				*pFileNameType = FileNameType::DirectoryObject;

				return STATUS_SUCCESS;
			}

			if (mCSDevice->headObject(CONT_CALLER objKey, &dirInfo))
			{
				// "\bucket\dir\file.txt" のパターン
				// 
				// ファイルを採用

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
		// "desktop.ini" などは無視させる

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