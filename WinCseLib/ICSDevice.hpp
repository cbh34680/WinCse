#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WCSE {

// 文字列をバケット名とキーに分割
class ObjectKey
{
private:
	std::wstring mBucket;
	std::wstring mKey;

	std::wstring mBucketKey;
	bool mHasBucket = false;
	bool mHasKey = false;
	bool mMeansDir = false;
	bool mMeansFile = false;

	WINCSELIB_API void reset() noexcept;

public:
	ObjectKey() = default;

	explicit ObjectKey(const std::wstring& argBucket, const std::wstring& argKey) noexcept
		:
		mBucket(argBucket),
		mKey(argKey)
	{
		reset();
	}

	// unorderd_map のキーになるために必要
	bool operator==(const ObjectKey& other) const noexcept
	{
		return mBucket == other.mBucket && mKey == other.mKey;
	}

	// map のキーになるために必要
	WINCSELIB_API bool operator<(const ObjectKey& other) const noexcept;

	// ObjectCacheKey で必要になった
	WINCSELIB_API bool operator>(const ObjectKey& other) const noexcept;

	// 長いので cpp
	WINCSELIB_API ObjectKey toFile() const noexcept;
	WINCSELIB_API ObjectKey toDir() const noexcept;
	WINCSELIB_API ObjectKey append(const std::wstring& arg) const noexcept;
	WINCSELIB_API std::unique_ptr<ObjectKey> toParentDir() const;

	// WC2MB() を使っているので cpp
	WINCSELIB_API std::string bucketA() const;
	WINCSELIB_API std::string keyA() const;
	WINCSELIB_API std::string strA() const;

	// inline
	const std::wstring& bucket() const noexcept { return mBucket; }
	const std::wstring& key() const noexcept { return mKey; }

	bool valid() const noexcept { return mHasBucket; }
	bool invalid() const noexcept { return !mHasBucket; }

	bool isBucket() const noexcept { return mHasBucket && !mHasKey; }
	bool isObject() const noexcept { return mHasBucket && mHasKey; }

	const std::wstring& str() const noexcept { return mBucketKey; }
	PCWSTR c_str() const noexcept { return mBucketKey.c_str(); }

	bool meansDir() const noexcept { return mMeansDir; }
	bool meansFile() const noexcept { return mMeansFile; }

	bool meansHidden() const noexcept
	{
		if (mHasKey)
		{
			// ".", ".." 以外で先頭が "." で始まっているものは隠しファイルの扱い

			if (mKey != L"." && mKey != L".." && mKey.at(0) == L'.')
			{
				return true;
			}
		}

		return false;
	}

	WINCSELIB_API static ObjectKey fromPath(const std::wstring& argPath);
	WINCSELIB_API static ObjectKey fromWinPath(const std::wstring& argWinPath);
};

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

	virtual DirInfoType headBucket(CALLER_ARG const std::wstring& argBucket) = 0;

	virtual bool listBuckets(CALLER_ARG DirInfoListType* pDirInfoList) = 0;

	virtual DirInfoType headObject(CALLER_ARG const ObjectKey& argObjKey) = 0;

	virtual bool listObjects(CALLER_ARG const ObjectKey& argObjKey,
		DirInfoListType* pDirInfoList /* nullable */) = 0;

	virtual bool listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey,
		DirInfoListType* pDirInfoList)
	{
		_ASSERT(pDirInfoList);

		return this->listObjects(CONT_CALLER argObjKey, pDirInfoList);
	}

	virtual CSDeviceContext* create(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 FileAttributes) = 0;

	virtual CSDeviceContext* open(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& FileInfo) = 0;

	virtual void close(CALLER_ARG CSDeviceContext* argCSDCtx) = 0;

	virtual NTSTATUS readObject(CALLER_ARG CSDeviceContext* argCSDCtx,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;

	virtual NTSTATUS writeObject(CALLER_ARG CSDeviceContext* argCSDCtx,
		PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile,
		BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) = 0;

	virtual bool deleteObject(CALLER_ARG const ObjectKey& argObjKey) = 0;

	virtual NTSTATUS renameObject(CALLER_ARG CSDeviceContext* argCSDCtx,
		const ObjectKey& argNewObjKey) = 0;

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