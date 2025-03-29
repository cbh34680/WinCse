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

// ��������o�P�b�g���ƃL�[�ɕ���
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

	// unorderd_map �̃L�[�ɂȂ邽�߂ɕK�v
	bool operator==(const ObjectKey& other) const noexcept
	{
		return mBucket == other.mBucket && mKey == other.mKey;
	}

	// map �̃L�[�ɂȂ邽�߂ɕK�v
	WINCSELIB_API bool operator<(const ObjectKey& other) const noexcept;

	// ObjectCacheKey �ŕK�v�ɂȂ���
	WINCSELIB_API bool operator>(const ObjectKey& other) const noexcept;

	// �����̂� cpp
	WINCSELIB_API ObjectKey toFile() const noexcept;
	WINCSELIB_API ObjectKey toDir() const noexcept;
	WINCSELIB_API ObjectKey append(const std::wstring& arg) const noexcept;
	WINCSELIB_API std::unique_ptr<ObjectKey> toParentDir() const;

	// WC2MB() ���g���Ă���̂� cpp
	WINCSELIB_API std::string bucketA() const;
	WINCSELIB_API std::string keyA() const;
	WINCSELIB_API std::string strA() const;

	// inline
	const std::wstring& bucket() const noexcept { return mBucket; }
	const std::wstring& key() const noexcept { return mKey; }

	bool valid() const noexcept { return mHasBucket; }
	bool invalid() const noexcept { return !mHasBucket; }
	bool hasKey() const noexcept { return mHasKey; }
	bool isBucket() const noexcept { return mHasBucket && !mHasKey; }
	const std::wstring& str() const noexcept { return mBucketKey; }
	const wchar_t* c_str() const noexcept { return mBucketKey.c_str(); }
	bool meansDir() const noexcept { return mMeansDir; }
	bool meansFile() const noexcept { return mMeansFile; }

	bool meansHidden() const noexcept
	{
		if (mHasKey)
		{
			// ".", ".." �ȊO�Ő擪�� "." �Ŏn�܂��Ă�����͉̂B���t�@�C���̈���

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

	WINCSELIB_API bool getCacheFilePath(std::wstring* pPath) const;

	bool isDir() const noexcept { return mIsDir; }
	bool isFile() const noexcept { return !mIsDir; }

	virtual ~CSDeviceContext() = default;
};

constexpr uint32_t CSDCTX_FLAGS_MODIFY			=     1;
constexpr uint32_t CSDCTX_FLAGS_WRITE			= 2 + 1;
constexpr uint32_t CSDCTX_FLAGS_OVERWRITE		= 4 + 1;
constexpr uint32_t CSDCTX_FLAGS_SET_BASIC_INFO	= 8 + 1;

struct ICSDevice : public ICSService
{
	virtual ~ICSDevice() = default;

	virtual void queryStats(WINCSE_DEVICE_STATS* pStats) = 0;

	virtual bool headBucket(CALLER_ARG const std::wstring& argBucket,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) = 0;

	virtual bool listBuckets(CALLER_ARG DirInfoListType* pDirInfoList,
		const std::vector<std::wstring>& options) = 0;

	virtual DirInfoType getBucket(CALLER_ARG const std::wstring& bucketName) = 0;

	virtual bool headObject(CALLER_ARG const ObjectKey& argObjKey,
		FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) = 0;

	virtual bool listObjects(CALLER_ARG const ObjectKey& argObjKey,
		DirInfoListType* pDirInfoList /* nullable */) = 0;

	virtual CSDeviceContext* create(CALLER_ARG const ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& fileInfo, const UINT32 CreateOptions,
		const UINT32 GrantedAccess, const UINT32 FileAttributes) = 0;

	virtual CSDeviceContext* open(CALLER_ARG const ObjectKey& argObjKey,
		const UINT32 CreateOptions, const UINT32 GrantedAccess,
		const FSP_FSCTL_FILE_INFO& FileInfo) = 0;

	virtual void close(CALLER_ARG CSDeviceContext* argCSDeviceContext) = 0;

	virtual NTSTATUS readObject(CALLER_ARG CSDeviceContext* argCSDeviceContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;

	virtual bool deleteObject(CALLER_ARG const ObjectKey& argObjKey) = 0;

	virtual bool putObject(CALLER_ARG const ObjectKey& argObjKey,
		const wchar_t* sourceFile, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */) = 0;

	virtual NTSTATUS getHandleFromContext(CALLER_ARG CSDeviceContext* argCSDeviceContext,
		const DWORD argDesiredAccess, const DWORD argCreationDisposition,
		HANDLE* pHandle) = 0;
};

} // namespace WinCseLib

  // �J�X�^���n�b�V���֐� ... unorderd_map �̃L�[�ɂȂ邽�߂ɕK�v
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