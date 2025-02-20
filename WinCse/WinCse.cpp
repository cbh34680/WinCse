#include "WinCseLib.h"
#include "WinCse.hpp"
#include <filesystem>

#undef traceA


std::atomic<int> LogBlock::mCounter(0);

WinCse::WinCse(const wchar_t* tmpdir, const wchar_t* iniSection,
	IWorker* delayedWorker, IWorker* idleWorker, ICloudStorage* storage) :
	mTempDir(tmpdir), mIniSection(iniSection),
	mDelayedWorker(delayedWorker),	mIdleWorker(idleWorker),
	mStorage(storage),
	mMaxFileSize(-1),
	mIgnoreFileNamePattern{ LR"(.*\\(desktop\.ini|autorun\.inf|thumbs\.db)$)", std::regex_constants::icase }
{
	NEW_LOG_BLOCK();

	APP_ASSERT(std::filesystem::exists(tmpdir));
	APP_ASSERT(std::filesystem::is_directory(tmpdir));
	APP_ASSERT(iniSection);
	APP_ASSERT(storage);
	APP_ASSERT(delayedWorker);
	APP_ASSERT(idleWorker);
}

WinCse::~WinCse()
{
	NEW_LOG_BLOCK();

	traceW(L"close handle");

	if (mFileRefHandle != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(mFileRefHandle);
		mFileRefHandle = INVALID_HANDLE_VALUE;
	}

	if (mDirRefHandle != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(mDirRefHandle);
		mDirRefHandle = INVALID_HANDLE_VALUE;
	}

	traceW(L"all done.");
}

bool WinCse::isIgnoreFileName(const wchar_t* arg)
{
	// desktop.ini などリクエストが増え過ぎるものは無視する

	return std::regex_match(std::wstring(arg), mIgnoreFileNamePattern);
}

//
// passthrough.c から拝借
//
NTSTATUS WinCse::HandleToInfo(HANDLE handle, PUINT32 PFileAttributes,
	PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize)
{
	NEW_LOG_BLOCK();

	FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo = {};
	DWORD SecurityDescriptorSizeNeeded = 0;

	if (0 != PFileAttributes)
	{
		if (!::GetFileInformationByHandleEx(handle,
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
		if (!::GetKernelObjectSecurity(handle,
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

NTSTATUS WinCse::FileNameToFileInfo(const wchar_t* FileName, FSP_FSCTL_FILE_INFO* pFileInfo)
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
		traceW(L"detect directory/1");

		GetFileInfoInternal(this->mDirRefHandle, &fileInfo);
	}
	else
	{
		// ここに来るのは "\\bucket" 又は "\\bucket\\key" のみ

		// DoGetSecurityByName() と同様の検査をして、その結果を PFileContext
		// と FileInfo に反映させる

		const BucketKey bk{ FileName };
		if (!bk.OK)
		{
			traceW(L"illegal FileName: %s", FileName);

			return STATUS_INVALID_PARAMETER;
		}

		if (bk.HasKey)
		{
			// "\bucket\key" のパターン

			// "key/" で一件のみ取得して、存在したらディレクトリが存在すると
			// 判定して、その情報をディレクトリ属性として採用

			std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

			//
			// SetDelimiter("/") を設定すると CommonPrefix で取得してしまい
			// CreationTime などが 0 になってしまうため false
			//
			if (mStorage->listObjects(INIT_CALLER bk.bucket, bk.key + L'/', &dirInfoList, 1, false))
			{
				APP_ASSERT(!dirInfoList.empty());
				APP_ASSERT(dirInfoList.size() == 1);

				// ディレクトリを採用
				isDir= true;
				traceW(L"detect directory/2");

				// ディレクトリの場合は FSP_FSCTL_FILE_INFO に適当な値を埋める
				// ... 取得した要素の情報([0]) がファイルの場合もあるので、編集が必要

				const auto& dirInfo(dirInfoList[0]);
				const auto FileTime = dirInfo->FileInfo.ChangeTime;

				fileInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;
				fileInfo.CreationTime = FileTime;
				fileInfo.LastAccessTime = FileTime;
				fileInfo.LastWriteTime = FileTime;
				fileInfo.ChangeTime = FileTime;
				fileInfo.IndexNumber = HashString(bk.bucket + L'/' + bk.key);
			}

			if (!isDir)
			{
				// ファイル名の完全一致で検索

				if (mStorage->headObject(INIT_CALLER bk.bucket, bk.key, &fileInfo))
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

			std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

			// 名前を指定してリストを取得

			const std::vector<std::wstring>& options = { bk.bucket };

			if (mStorage->listBuckets(INIT_CALLER &dirInfoList, options))
			{
				APP_ASSERT(!dirInfoList.empty());

				// ディレクトリを採用
				isDir = true;
				traceW(L"detect directory/3");

				fileInfo = dirInfoList[0]->FileInfo;
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