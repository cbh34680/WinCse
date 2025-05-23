#pragma once

namespace CSELIB {

using DirInfoType = std::unique_ptr<FSP_FSCTL_DIR_INFO, void(*)(void*)>;

class DirectoryEntry;
using DirEntryType = std::shared_ptr<DirectoryEntry>;
using DirEntryListType = std::list<DirEntryType>;

class DirectoryEntry final
{
private:
	static std::atomic<int>	mLastInstanceId;
	const int				mInstanceId;

public:
	const std::wstring		mName;
	const FileTypeEnum		mFileType;

	FSP_FSCTL_FILE_INFO						mFileInfo{};		// äOïîÇ©ÇÁíºê⁄ïœçXÇ≥ÇÍÇÈÇ‡ÇÃ
	std::map<std::wstring, std::wstring>	mUserProperties;	// ÅV

	DirectoryEntry(FileTypeEnum argFileType, const std::wstring& argName, UINT64 argFileSize, FILETIME_100NS_T argFileTime)
		:
		DirectoryEntry(argFileType, argName, argFileSize, argFileTime, argFileTime, argFileTime, argFileTime)
	{
	}

	WINCSELIB_API DirectoryEntry(FileTypeEnum argFileType, const std::wstring& argName, UINT64 argFileSize, FILETIME_100NS_T argCreationTime, FILETIME_100NS_T argLastAccessTime, FILETIME_100NS_T argLastWriteTime, FILETIME_100NS_T argChangeTime);

	WINCSELIB_API const std::wstring str() const;
	WINCSELIB_API std::wstring getFileNameBuf() const;

	WINCSELIB_API DirInfoType makeDirInfo() const;
	WINCSELIB_API void getDirInfo(FSP_FSCTL_DIR_INFO* pDirInfo) const;

	WINCSELIB_API static DirEntryType makeRootEntry(FILETIME_100NS_T argFileTime);
	WINCSELIB_API static DirEntryType makeBucketEntry(const std::wstring& argName, FILETIME_100NS_T argFileTime);
	WINCSELIB_API static DirEntryType makeDotEntry(const std::wstring& argName, FILETIME_100NS_T argFileTime);

	WINCSELIB_API static DirEntryType makeDirectoryEntry(const std::wstring& argName, FILETIME_100NS_T argCreationTime, FILETIME_100NS_T argLastAccessTime, FILETIME_100NS_T argLastWriteTime, FILETIME_100NS_T argChangeTime);
	WINCSELIB_API static DirEntryType makeDirectoryEntry(const std::wstring& argName, FILETIME_100NS_T argFileTime);

	WINCSELIB_API static DirEntryType makeFileEntry(const std::wstring& argName, UINT64 argFileSize, FILETIME_100NS_T argCreationTime, FILETIME_100NS_T argLastAccessTime, FILETIME_100NS_T argLastWriteTime, FILETIME_100NS_T argChangeTime);
	WINCSELIB_API static DirEntryType makeFileEntry(const std::wstring& argName, UINT64 argFileSize, FILETIME_100NS_T argFileTime);

};

}	// namespace CSELIB

// EOF