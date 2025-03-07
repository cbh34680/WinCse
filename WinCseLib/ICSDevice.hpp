#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

typedef struct
{
	long OnSvcStart;
	long OnSvcStop;

	long headBucket;
	long listBuckets;
	long headObject;
	long listObjects;
	long openFile;
	long readFile;
	long closeFile;

	long _CreateFile;
	long _CloseHandle_File;
	long _CreateEvent;
	long _CloseHandle_Event;

	long _ReadSuccess;
	long _ReadError;

	long _unsafeHeadObject_File;
	long _unsafeListObjects_Dir;
	long _unsafeListObjects_Display;
	long _findFileInParentDirectry;
}
WINCSE_DEVICE_STATS;

namespace WinCseLib {

struct WINCSELIB_API ICSDevice : public ICSService
{
	virtual ~ICSDevice() = default;

	virtual void queryStats(WINCSE_DEVICE_STATS* pStats) = 0;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket) = 0;

	virtual bool listBuckets(CALLER_ARG
		DirInfoListType* pDirInfoList,
		const std::vector<std::wstring>& options) = 0;

	virtual bool headObject(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		FSP_FSCTL_FILE_INFO* pFileInfo) = 0;

	virtual bool listObjects(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		DirInfoListType* pDirInfoList) = 0;

	virtual bool openFile(CALLER_ARG
		const std::wstring& argBucket, const std::wstring& argKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& fileInfo, 
		PVOID* pUParam) = 0;

	virtual void closeFile(CALLER_ARG PVOID UParam) = 0;

	virtual bool readFile(CALLER_ARG PVOID UParam,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;
};

} // namespace WinCseLib

#pragma warning(pop)
// EOF