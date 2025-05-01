#include "CSDeviceCommon.h"

using namespace CSELIB;


namespace CSEDAS3
{

DirInfoPtr makeDirInfoOfDir(const std::wstring& argFileName, FILETIME_100NS_T argFileTime100ns, UINT32 argFileAttributes)
{
	APP_ASSERT(!argFileName.empty());

	FSP_FSCTL_FILE_INFO fileInfo{};
	UINT32 fileAttributes = FILE_ATTRIBUTE_DIRECTORY | argFileAttributes;

	if (MeansHiddenFile(argFileName))
	{
		// ‰B‚µƒtƒ@ƒCƒ‹

		fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
	}

	fileInfo.FileAttributes = fileAttributes;
	fileInfo.CreationTime = argFileTime100ns;
	fileInfo.LastAccessTime = argFileTime100ns;
	fileInfo.LastWriteTime = argFileTime100ns;

	return allocBasicDirInfo(argFileName, FileTypeEnum::DirectoryObject, fileInfo);
}

}	// namespace CSEDAS3

// EOF