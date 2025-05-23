#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace CSELIB {

struct ICSDevice : public ICSService
{
	virtual bool shouldIgnoreWinPath(const std::filesystem::path& argWinPath) = 0;
	virtual void printReport(FILE* fp) = 0;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket, DirEntryType* pDirEntry) = 0;
	virtual bool listBuckets(CALLER_ARG DirEntryListType* pDirEntryList) = 0;
	virtual bool headObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry) = 0;

	virtual bool headObjectAsDirectory(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
	{
		return this->headObject(CONT_CALLER argObjKey.toDir(), pDirEntry);
	}

	virtual bool listObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList) = 0;

	virtual bool listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
	{
		APP_ASSERT(pDirEntryList);

		return this->listObjects(CONT_CALLER argObjKey, pDirEntryList);
	}

	virtual FILEIO_LENGTH_T getObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength) = 0;
	virtual bool putObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath) = 0;
	virtual bool copyObject(CALLER_ARG const ObjectKey& argSrcObjKey, const ObjectKey& argDstObjKey) = 0;
	virtual bool deleteObject(CALLER_ARG const ObjectKey& argObjKey) = 0;
	virtual bool deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) = 0;
};

} // namespace CSELIB

#pragma warning(pop)
// EOF