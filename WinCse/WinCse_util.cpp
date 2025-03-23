#include "WinCseLib.h"
#include "WinCse.hpp"
#include <sstream>


using namespace WinCseLib;


bool WinCse::isFileNameIgnored(const std::wstring& arg)
{
	// desktop.ini などリクエストが増え過ぎるものは無視する

	if (mIgnoredFileNamePatterns.mark_count() == 0)
	{
		// 正規表現が設定されていない
		return false;
	}

	return std::regex_search(arg, mIgnoredFileNamePatterns);
}

//
// passthrough.c から拝借
//
NTSTATUS WinCse::HandleToInfo(CALLER_ARG HANDLE hFile, PUINT32 PFileAttributes /* nullable */,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize /* nullable */)
{
	NEW_LOG_BLOCK();

	FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo{};
	DWORD SecurityDescriptorSizeNeeded = 0;

	if (0 != PFileAttributes)
	{
		if (!::GetFileInformationByHandleEx(hFile,
			FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
		{
			return FspNtStatusFromWin32(::GetLastError());
		}

		traceW(L"FileAttributes: %u", AttributeTagInfo.FileAttributes);
		traceW(L"\tdetect: %s", FA_IS_DIR(AttributeTagInfo.FileAttributes) ? L"dir" : L"file");

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
		// "\" へのアクセスは参照用ディレクトリの情報を提供

		isDir = true;
		traceW(L"detect directory(1)");

		NTSTATUS ntstatus = GetFileInfoInternal(this->mRefDir.handle(), &fileInfo);
		if (!NT_SUCCESS(ntstatus))
		{
			traceW(L"fault: GetFileInfoInternal");
			return ntstatus;
		}
	}
	else
	{
		// ここに来るのは "\\bucket" 又は "\\bucket\\key" のみ

		// DoGetSecurityByName() と同様の検査をして、その結果を PFileContext
		// と FileInfo に反映させる

		const ObjectKey objKey{ ObjectKey::fromWinPath(FileName) };
		if (objKey.invalid())
		{
			traceW(L"illegal FileName: %s", FileName);

			return STATUS_OBJECT_NAME_INVALID;
		}

		if (objKey.hasKey())
		{
			// "\bucket\dir" のパターン

			if (mCSDevice->headObject(CONT_CALLER objKey.toDir(), &fileInfo))
			{
				// ディレクトリを採用

				isDir= true;
				traceW(L"detect directory(2)");
			}
			else
			{
				// "\bucket\dir\file.txt" のパターン

				if (mCSDevice->headObject(CONT_CALLER objKey, &fileInfo))
				{
					// ファイルを採用

					isFile = true;
					traceW(L"detect file");
				}
			}
		}
		else
		{
			// "\bucket" のパターン

			// HeadBucket ではメタ情報が取得できないので ListBuckets から名前が一致するものを取得

			DirInfoListType dirInfoList;

			// 名前を指定してリストを取得

			if (mCSDevice->listBuckets(CONT_CALLER &dirInfoList, { objKey.bucket() }))
			{
				APP_ASSERT(dirInfoList.size() == 1);

				// ディレクトリを採用

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

// EOF