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
	long create;
	long open;
	long readObject;
	long close;
	long cleanup;
}
WINCSE_DEVICE_STATS;

namespace WinCseLib {

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

	WINCSELIB_API ObjectKey(const std::wstring& argWinPath);

public:
	ObjectKey() = default;
	ObjectKey(const std::wstring& argBucket, const std::wstring& argKey)
		: mBucket(argBucket), mKey(argKey)
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
	WINCSELIB_API ObjectKey toDir() const noexcept;
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
	bool hasKey() const noexcept { return mHasKey; }
	const std::wstring& str() const noexcept { return mBucketKey; }
	const wchar_t* c_str() const noexcept { return mBucketKey.c_str(); }
	bool meansDir() const noexcept { return mMeansDir; }
	bool meansFile() const noexcept { return mMeansFile; }

	bool meansHidden() const noexcept
	{
		if (mHasKey)
		{
			// ".", ".." 以外で先頭が "." で始まっているものは隠しファイルの扱い

			if (mKey != L"." && mKey != L".." && mKey[0] == L'.')
			{
				return true;
			}
		}

		return false;
	}

	static ObjectKey fromWinPath(const std::wstring& argWinPath)
	{
		return ObjectKey(argWinPath);
	}
};

struct CSDeviceContext
{
	const std::wstring mCacheDataDir;
	ObjectKey mObjKey;
	const FSP_FSCTL_FILE_INFO mFileInfo;
	const bool mIsDir;
	FileHandle mFile;
	uint32_t mFlags = 0U;

	WINCSELIB_API CSDeviceContext(const std::wstring& argCacheDataDir,
		const WinCseLib::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo);

	WINCSELIB_API bool getFilePathW(std::wstring* pPath) const;
	WINCSELIB_API bool getFilePathA(std::string* pPath) const;

	bool isDir() const noexcept { return mIsDir; }
	bool isFile() const noexcept { return !mIsDir; }
	std::wstring getRemotePath() const noexcept { return mObjKey.str(); }

	virtual ~CSDeviceContext() = default;
};

constexpr uint32_t CSDCTX_FLAGS_WRITE = 1;
constexpr uint32_t CSDCTX_FLAGS_DELETE = 2;

struct ICSDevice : public ICSService
{
	virtual ~ICSDevice() = default;

	virtual void queryStats(WINCSE_DEVICE_STATS* pStats) = 0;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket) = 0;

	virtual bool listBuckets(CALLER_ARG DirInfoListType* pDirInfoList,
		const std::vector<std::wstring>& options) = 0;

	virtual bool headObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;

	virtual bool listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList) = 0;

	virtual CSDeviceContext* create(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 FileAttributes,
		PSECURITY_DESCRIPTOR SecurityDescriptor, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;

	virtual CSDeviceContext* open(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& FileInfo) = 0;

	virtual void close(CALLER_ARG CSDeviceContext* argCSDeviceContext) = 0;

	virtual NTSTATUS readObject(CALLER_ARG CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;

	virtual bool putObject(CALLER_ARG const WinCseLib::ObjectKey& argObjKey,
		const char* sourceFile, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) = 0;

	virtual void cleanup(CALLER_ARG CSDeviceContext* argCSDeviceContext, ULONG Flags) = 0;

	virtual NTSTATUS getHandleFromContext(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
		const DWORD argDesiredAccess, const DWORD argCreationDisposition,
		HANDLE* pHandle) = 0;
};

} // namespace WinCseLib

  // カスタムハッシュ関数 ... unorderd_map のキーになるために必要
namespace std
{
template <>
struct hash<WinCseLib::ObjectKey>
{
	size_t operator()(const WinCseLib::ObjectKey& that) const noexcept
	{
		return hash<wstring>()(that.bucket()) ^ (hash<wstring>()(that.key()) << 1);
	}
};
}

#pragma warning(pop)
// EOF