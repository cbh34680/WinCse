#include "WinCseLib.h"

using namespace CSELIB;


std::atomic<int> DirectoryEntry::mLastInstanceId = 0;

DirectoryEntry::DirectoryEntry(FileTypeEnum argFileType, const std::wstring& argName, UINT64 argFileSize, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime)
	:
	mInstanceId(++mLastInstanceId),
	mFileType(argFileType),
	mName(argName)
{
	APP_ASSERT(mName.find(L'\\') == std::wstring::npos);

	switch (argFileType)
	{
		case FileTypeEnum::Root:
		case FileTypeEnum::Bucket:
		case FileTypeEnum::Directory:
		{
			// ルート、バケット、ディレクトリはディレクトリ属性を設定

			mFileInfo.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

			break;
		}
	}

	switch (argFileType)
	{
		case FileTypeEnum::Directory:
		case FileTypeEnum::File:
		{
			if (mName != L"." && mName != L".." && mName.at(0) == L'.')
			{
				// ディレクトリ、ファイルの先頭が "." のものは隠し属性を設定

				mFileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
			}

			break;
		}
	}

	switch (argFileType)
	{
		case FileTypeEnum::File:
		{
			if (!mFileInfo.FileAttributes)
			{
				mFileInfo.FileAttributes |= FILE_ATTRIBUTE_NORMAL;
			}

			// アロケーション・サイズを算出

			mFileInfo.FileSize = argFileSize;

			if (argFileSize > 0)
			{
				mFileInfo.AllocationSize = ALIGN_TO_UNIT(argFileSize, ALLOCATION_UNIT);
			}

			break;
		}
	}

	mFileInfo.CreationTime = argCreationTime;
	mFileInfo.LastAccessTime = argLastAccessTime;
	mFileInfo.LastWriteTime = argLastWriteTime;
	mFileInfo.ChangeTime = argChangeTime;

	mFileInfo.IndexNumber = HashString(mName);
}

const std::wstring DirectoryEntry::str() const
{
	LastErrorBackup _backup;

	std::wstringstream ss;

	ss << L"mInstanceId=" << mInstanceId;
	ss << L" mName=" << mName;
	ss << L" mFileType=" << FileTypeEnumToStringW(mFileType);
	ss << L" mFileInfo=" << FileInfoToStringW(mFileInfo);

	for (const auto& it: mUserProperties)
	{
		ss << L" P." << it.first << L"=" << it.second;
	}

	return ss.str();
}

std::wstring DirectoryEntry::getFileNameBuf() const
{
	std::wstring fileNameBuf{ mName };

	switch (mFileType)
	{
		case FileTypeEnum::Root:
		{
			APP_ASSERT(0);

			fileNameBuf = L"@";
			break;
		}

		case FileTypeEnum::Directory:
		{
			if (fileNameBuf != L"." && fileNameBuf != L"..")
			{
				fileNameBuf.pop_back();
			}

			break;
		}
	}

	APP_ASSERT(fileNameBuf.find(L'/') == std::wstring::npos);

	return fileNameBuf;
}

void DirectoryEntry::getDirInfo(FSP_FSCTL_DIR_INFO* pDirInfo) const
{
	const auto fileNameBuf = this->getFileNameBuf();

	const auto keyLen = fileNameBuf.length();
	const auto keyLenBytes = keyLen * sizeof(WCHAR);

	const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
	const auto dirInfoSize = static_cast<UINT16>(offFileNameBuf + keyLenBytes);

	pDirInfo->Size = dirInfoSize;
	pDirInfo->FileInfo = mFileInfo;

	memcpy(pDirInfo->FileNameBuf, fileNameBuf.c_str(), keyLenBytes);
}

DirInfoType DirectoryEntry::makeDirInfo() const
{
	// FSP_FSCTL_DIR_INFO を生成

	const auto fileNameBuf = this->getFileNameBuf();

	const auto keyLen = fileNameBuf.length();
	const auto keyLenBytes = keyLen * sizeof(WCHAR);

	const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
	const auto dirInfoSize = static_cast<UINT16>(offFileNameBuf + keyLenBytes);

	// 一応 nullterm させているが、なくても問題なさそう

	const auto allocSize = dirInfoSize + /* nullterm */ sizeof(WCHAR);

	FSP_FSCTL_DIR_INFO* dirInfoRaw = (FSP_FSCTL_DIR_INFO*)calloc(1, allocSize);
	APP_ASSERT(dirInfoRaw);

	dirInfoRaw->Size = dirInfoSize;
	dirInfoRaw->FileInfo = mFileInfo;

	//
	// 実行時にエラーとなる (argBuffer is too small)
	// 
	// おそらく、FSP_FSCTL_DIR_INFO.FileNameBuf は [] として定義されているため
	// wcscpy_s では 0 byte 領域へのバッファ・オーバーフローとして認識されて
	// しまうのではないかと思う
	// 
	//wcscpy_s(dirEntry->FileNameBuf, wkeyLen, wkey.c_str());

	memcpy(dirInfoRaw->FileNameBuf, fileNameBuf.c_str(), keyLenBytes);

	return DirInfoType{ dirInfoRaw, free };
}

DirEntryType DirectoryEntry::makeRootEntry(UINT64 argFileTime)
{
	APP_ASSERT(argFileTime);

	return std::make_shared<DirectoryEntry>(FileTypeEnum::Root, L"/", 0, argFileTime);
}

DirEntryType DirectoryEntry::makeBucketEntry(const std::wstring& argName, UINT64 argFileTime)
{
	APP_ASSERT(argFileTime);
	APP_ASSERT(!argName.empty());
	APP_ASSERT(argName.find(L'/') == std::wstring::npos);

	return std::make_shared<DirectoryEntry>(FileTypeEnum::Bucket, argName, 0, argFileTime);
}

DirEntryType DirectoryEntry::makeDotEntry(const std::wstring& argName, UINT64 argFileTime)
{
	APP_ASSERT(argFileTime);
	APP_ASSERT(argName == L"." || argName == L"..");

	return std::make_shared<DirectoryEntry>(FileTypeEnum::Directory, argName, 0, argFileTime);
}

DirEntryType DirectoryEntry::makeDirectoryEntry(const std::wstring& argName, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime)
{
	APP_ASSERT(argCreationTime);
	APP_ASSERT(argLastAccessTime);
	APP_ASSERT(argLastWriteTime);
	APP_ASSERT(argChangeTime);
	APP_ASSERT(!argName.empty());
	APP_ASSERT(argName != L"." && argName != L".." && argName != L"/");
	APP_ASSERT(argName.back() == L'/');

	return std::make_shared<DirectoryEntry>(FileTypeEnum::Directory, argName, 0, argCreationTime, argLastAccessTime, argLastWriteTime, argChangeTime);
}

DirEntryType DirectoryEntry::makeDirectoryEntry(const std::wstring& argName, UINT64 argFileTime)
{
	APP_ASSERT(argFileTime);
	APP_ASSERT(!argName.empty());
	APP_ASSERT(argName != L"." && argName != L".." && argName != L"/");
	APP_ASSERT(argName.back() == L'/');

	return std::make_shared<DirectoryEntry>(FileTypeEnum::Directory, argName, 0, argFileTime);
}

DirEntryType DirectoryEntry::makeFileEntry(const std::wstring& argName, UINT64 argFileSize, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime)
{
	APP_ASSERT(argCreationTime);
	APP_ASSERT(argLastAccessTime);
	APP_ASSERT(argLastWriteTime);
	APP_ASSERT(argChangeTime);
	APP_ASSERT(!argName.empty());
	APP_ASSERT(argName.find(L'/') == std::wstring::npos);

	return std::make_shared<DirectoryEntry>(FileTypeEnum::File, argName, argFileSize, argCreationTime, argLastAccessTime, argLastWriteTime, argChangeTime);
}

DirEntryType DirectoryEntry::makeFileEntry(const std::wstring& argName, UINT64 argFileSize, UINT64 argFileTime)
{
	APP_ASSERT(argFileTime);
	APP_ASSERT(!argName.empty());
	APP_ASSERT(argName.find(L'/') == std::wstring::npos);

	return std::make_shared<DirectoryEntry>(FileTypeEnum::File, argName, argFileSize, argFileTime);
}

// EOF