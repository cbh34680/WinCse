#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WCSE {

struct CSDeviceContext
{
	const std::wstring mCacheDataDir;
	const ObjectKey mObjKey;

	FSP_FSCTL_FILE_INFO mFileInfo;
	FileHandle mFile;
	uint32_t mFlags = 0U;

	WINCSELIB_API explicit CSDeviceContext(const std::wstring& argCacheDataDir,
		const WCSE::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo) noexcept;

	WINCSELIB_API std::wstring getCacheFilePath() const;

	WINCSELIB_API bool isDir() const noexcept;
	bool isFile() const noexcept { return !isDir(); }

	virtual ~CSDeviceContext() = default;
};

constexpr uint32_t CSDCTX_FLAGS_MODIFY				= 1;
constexpr uint32_t CSDCTX_FLAGS_READ				= 2;

constexpr uint32_t CSDCTX_FLAGS_M_CREATE			= 0x0100;
constexpr uint32_t CSDCTX_FLAGS_M_WRITE				= CSDCTX_FLAGS_M_CREATE << 1;
constexpr uint32_t CSDCTX_FLAGS_M_OVERWRITE			= CSDCTX_FLAGS_M_CREATE << 2;
constexpr uint32_t CSDCTX_FLAGS_M_SET_BASIC_INFO	= CSDCTX_FLAGS_M_CREATE << 4;
constexpr uint32_t CSDCTX_FLAGS_M_SET_FILE_SIZE		= CSDCTX_FLAGS_M_CREATE << 8;

constexpr uint32_t CSDCTX_FLAGS_CREATE				= CSDCTX_FLAGS_MODIFY | CSDCTX_FLAGS_M_CREATE;
constexpr uint32_t CSDCTX_FLAGS_WRITE				= CSDCTX_FLAGS_MODIFY | CSDCTX_FLAGS_M_WRITE;
constexpr uint32_t CSDCTX_FLAGS_OVERWRITE			= CSDCTX_FLAGS_MODIFY | CSDCTX_FLAGS_M_OVERWRITE;
constexpr uint32_t CSDCTX_FLAGS_SET_BASIC_INFO		= CSDCTX_FLAGS_MODIFY | CSDCTX_FLAGS_M_SET_BASIC_INFO;
constexpr uint32_t CSDCTX_FLAGS_SET_FILE_SIZE		= CSDCTX_FLAGS_MODIFY | CSDCTX_FLAGS_M_SET_FILE_SIZE;

struct ICSDevice : public ICSService
{
	virtual ~ICSDevice() = default;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket, DirInfoType* pDirInfo) = 0;

	virtual bool listBuckets(CALLER_ARG DirInfoListType* pDirInfoList) = 0;

	virtual bool headObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo) = 0;

	virtual bool listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList) = 0;

	virtual bool listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
	{
		_ASSERT(pDirInfoList);

		return this->listObjects(CONT_CALLER argObjKey, pDirInfoList);
	}

	virtual CSDeviceContext* create(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 FileAttributes) = 0;

	virtual CSDeviceContext* open(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo) = 0;

	virtual void close(CALLER_ARG CSDeviceContext* argCSDCtx) = 0;

	virtual NTSTATUS readObject(CALLER_ARG CSDeviceContext* argCSDCtx,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;

	virtual NTSTATUS writeObject(CALLER_ARG CSDeviceContext* argCSDCtx,
		PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile,
		BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) = 0;

	virtual NTSTATUS setDelete(CALLER_ARG CSDeviceContext* argCSDCtx, BOOLEAN argDeleteFile) = 0;

	virtual NTSTATUS renameObject(CALLER_ARG CSDeviceContext* argCSDCtx,
		const ObjectKey& argNewObjKey) = 0;

	virtual bool deleteObject(CALLER_ARG const ObjectKey& argObjKey) = 0;

	virtual NTSTATUS getHandleFromContext(CALLER_ARG CSDeviceContext* argCSDCtx,
		const DWORD argDesiredAccess, const DWORD argCreationDisposition, PHANDLE pHandle) = 0;
};

} // namespace WCSE

  // カスタムハッシュ関数 ... unorderd_map のキーになるために必要
namespace std
{
template <>
struct hash<WCSE::ObjectKey>
{
	size_t operator()(const WCSE::ObjectKey& that) const noexcept
	{
		return hash<wstring>()(that.bucket()) ^ (hash<wstring>()(that.key()) << 1);
	}
};
}

#pragma warning(pop)
// EOF