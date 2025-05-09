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

	DirectoryEntry(FileTypeEnum argFileType, const std::wstring& argName, UINT64 argFileSize, UINT64 argFileTime)
		:
		DirectoryEntry(argFileType, argName, argFileSize, argFileTime, argFileTime, argFileTime, argFileTime)
	{
	}

	WINCSELIB_API DirectoryEntry(FileTypeEnum argFileType, const std::wstring& argName, UINT64 argFileSize, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime);

	WINCSELIB_API const std::wstring str() const;
	WINCSELIB_API std::wstring getFileNameBuf() const;

	WINCSELIB_API DirInfoType makeDirInfo() const;
	WINCSELIB_API void getDirInfo(FSP_FSCTL_DIR_INFO* pDirInfo) const;

	WINCSELIB_API static DirEntryType makeRootEntry(UINT64 argFileTime);
	WINCSELIB_API static DirEntryType makeBucketEntry(const std::wstring& argName, UINT64 argFileTime);
	WINCSELIB_API static DirEntryType makeDotEntry(const std::wstring& argName, UINT64 argFileTime);

	WINCSELIB_API static DirEntryType makeDirectoryEntry(const std::wstring& argName, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime);
	WINCSELIB_API static DirEntryType makeDirectoryEntry(const std::wstring& argName, UINT64 argFileTime);

	WINCSELIB_API static DirEntryType makeFileEntry(const std::wstring& argName, UINT64 argFileSize, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime);
	WINCSELIB_API static DirEntryType makeFileEntry(const std::wstring& argName, UINT64 argFileSize, UINT64 argFileTime);

};

}	// namespace CSELIB

// EOF