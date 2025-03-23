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
	long remove;
	long writeObject;
}
WINCSE_DEVICE_STATS;

#define OBJECT_KEY_UNORDERED_MAP		(0)

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
	bool operator==(const ObjectKey& other) const
	{
		return mBucket == other.mBucket && mKey == other.mKey;
	}

	// map のキーになるために必要
	WINCSELIB_API bool operator<(const ObjectKey& other) const;

	// ObjectCacheKey で必要になった
	WINCSELIB_API bool operator>(const ObjectKey& other) const;

	// 長いので cpp
	WINCSELIB_API ObjectKey toDir() const;
	WINCSELIB_API std::unique_ptr<ObjectKey> toParentDir() const;

	// WC2MB() を使っているので cpp
	WINCSELIB_API std::string bucketA() const;
	WINCSELIB_API std::string keyA() const;
	WINCSELIB_API std::string strA() const;

	// inline
	const std::wstring& bucket() const { return mBucket; }
	const std::wstring& key() const { return mKey; }

	bool valid() const { return mHasBucket; }
	bool invalid() const { return !mHasBucket; }
	bool hasKey() const { return mHasKey; }
	const std::wstring& str() const { return mBucketKey; }
	const wchar_t* c_str() const { return mBucketKey.c_str(); }
	bool meansDir() const { return mMeansDir; }
	bool meansFile() const { return mMeansFile; }

	bool meansHidden() const
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
	FileHandle mFile;

	WINCSELIB_API CSDeviceContext(
		const std::wstring& argCacheDataDir,
		const WinCseLib::ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& argFileInfo);

	WINCSELIB_API bool isDir() const;
	WINCSELIB_API std::wstring getFilePathW() const;
	WINCSELIB_API std::string getFilePathA() const;

	bool isFile() const { return !isDir(); }
	std::wstring getRemotePath() const { return mObjKey.str(); }

	virtual ~CSDeviceContext() = default;
};

struct WINCSELIB_API ICSDevice : public ICSService
{
	virtual ~ICSDevice() = default;

	virtual void queryStats(WINCSE_DEVICE_STATS* pStats) = 0;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket) = 0;

	virtual bool listBuckets(CALLER_ARG
		DirInfoListType* pDirInfoList,
		const std::vector<std::wstring>& options) = 0;

	virtual bool headObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo) = 0;

	virtual bool listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList) = 0;

	virtual CSDeviceContext* create(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 FileAttributes,
		FSP_FSCTL_FILE_INFO* pFileInfo) = 0;

	virtual CSDeviceContext* open(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& FileInfo) = 0;

	virtual void close(CALLER_ARG CSDeviceContext* argCSDeviceContext) = 0;

	virtual NTSTATUS readObject(CALLER_ARG CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;

	virtual NTSTATUS writeObject(CALLER_ARG CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length,
		BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
		PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) = 0;

	virtual NTSTATUS remove(CALLER_ARG CSDeviceContext* argCSDeviceContext, BOOLEAN argDeleteFile) = 0;

	virtual void cleanup(CALLER_ARG CSDeviceContext* argCSDeviceContext, ULONG argFlags) = 0;
};

} // namespace WinCseLib

  // カスタムハッシュ関数 ... unorderd_map のキーになるために必要
namespace std
{
template <>
struct hash<WinCseLib::ObjectKey>
{
	size_t operator()(const WinCseLib::ObjectKey& that) const
	{
		return hash<wstring>()(that.bucket()) ^ (hash<wstring>()(that.key()) << 1);
	}
};
}

#pragma warning(pop)
// EOF