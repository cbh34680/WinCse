#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace CSELIB {

struct ICSDevice : public ICSService
{
	virtual bool shouldIgnoreFileName(const std::wstring& arg) = 0;
	virtual void printReport(FILE* fp) = 0;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket, DirInfoPtr* pDirInfo) = 0;
	virtual bool listBuckets(CALLER_ARG DirInfoPtrList* pDirInfoList) = 0;
	virtual bool headObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) = 0;
	virtual bool listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtrList* pDirInfoList) = 0;

	virtual bool listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtrList* pDirInfoList)
	{
		APP_ASSERT(pDirInfoList);

		return this->listObjects(CONT_CALLER argObjKey, pDirInfoList);
	}

	virtual CSELIB::FILEIO_LENGTH_T getObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) = 0;
	virtual bool putObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath) = 0;
};

} // namespace CSELIB

#pragma warning(pop)
// EOF