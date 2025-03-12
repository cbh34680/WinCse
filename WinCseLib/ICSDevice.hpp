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
	long open;
	long read;
	long close;
	long cleanup;
	long remove;

	long _CreateFile;
	long _CloseHandle_File;
	long _CreateEvent;
	long _CloseHandle_Event;

	long _ReadSuccess;
	long _ReadError;

	long _unlockHeadObject_File;
	long _unlockListObjects_Dir;
	long _unlockListObjects_Display;
	long _unlockFindInParentOfDisplay;
}
WINCSE_DEVICE_STATS;

#define OBJECT_KEY_UNORDERED_MAP		(0)

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

	WINCSELIB_API void reset();

	WINCSELIB_API ObjectKey(const std::wstring& argWinPath);

public:
	ObjectKey() = default;
	ObjectKey(const std::wstring& argBucket, const std::wstring& argKey)
		: mBucket(argBucket), mKey(argKey)
	{
		reset();
	}

	// unorderd_map �̃L�[�ɂȂ邽�߂ɕK�v
	bool operator==(const ObjectKey& other) const
	{
		return mBucket == other.mBucket && mKey == other.mKey;
	}

	// map �̃L�[�ɂȂ邽�߂ɕK�v
	WINCSELIB_API bool operator<(const ObjectKey& other) const;

	// ObjectCacheKey �ŕK�v�ɂȂ���
	WINCSELIB_API bool operator>(const ObjectKey& other) const;

	// �����̂� cpp
	WINCSELIB_API ObjectKey toDir() const;
	WINCSELIB_API std::unique_ptr<ObjectKey> toParentDir() const;

	// WC2MB() ���g���Ă���̂� cpp
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

	static ObjectKey fromWinPath(const std::wstring& argWinPath)
	{
		return ObjectKey(argWinPath);
	}
};

struct IOpenContext
{
	virtual ~IOpenContext() = default;
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

	virtual IOpenContext* open(CALLER_ARG const ObjectKey& argObjKey,
		const FSP_FSCTL_FILE_INFO& FileInfo,
		const UINT32 CreateOptions, const UINT32 GrantedAccess) = 0;

	virtual void close(CALLER_ARG IOpenContext* argOpenContext) = 0;

	virtual NTSTATUS readObject(CALLER_ARG IOpenContext* argOpenContext,
		PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) = 0;

	virtual NTSTATUS remove(CALLER_ARG IOpenContext* argOpenContext, BOOLEAN argDeleteFile) = 0;

	virtual void cleanup(CALLER_ARG IOpenContext* argOpenContext, ULONG argFlags) = 0;
};

} // namespace WinCseLib

  // �J�X�^���n�b�V���֐� ... unorderd_map �̃L�[�ɂȂ邽�߂ɕK�v
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