#pragma once

#include <atomic>

// AwsS3_obj_read.cpp, AwsS3_obj_r_single,cpp, AwsS3_obj_r_multiple.cpp
// ����̂� include ����邱��

struct SharedBase
{
	HANDLE mLock;
	int mCount = 0;

	SharedBase()
	{
		mLock = ::CreateMutexW(NULL, FALSE, NULL);
		APP_ASSERT(mLock);
	}

	virtual ~SharedBase()
	{
		::CloseHandle(mLock);
	}
};

template<typename T, typename... Args>
T* getUnprotectedNamedDataByName(const std::wstring& name, Args... args);
void releaseUnprotectedNamedDataByName(const std::wstring& name);

template<typename T>
struct UnprotectedNamedData
{
	const std::wstring mName;
	T* mLocal = nullptr;

	template<typename... Args>
	UnprotectedNamedData(const std::wstring& argName, Args... args) : mName(argName)
	{
		mLocal = getUnprotectedNamedDataByName<T>(mName, args...);
	}

	~UnprotectedNamedData()
	{
		releaseUnprotectedNamedDataByName(mName);
	}
};

template<typename T>
struct ProtectedNamedData
{
	UnprotectedNamedData<T>& mUnprotectedNamedData;

	ProtectedNamedData(UnprotectedNamedData<T>& argUnprotectedNamedData)
		: mUnprotectedNamedData(argUnprotectedNamedData)
	{
		const auto reason = ::WaitForSingleObject(mUnprotectedNamedData.mLocal->mLock, INFINITE);
		APP_ASSERT(reason == WAIT_OBJECT_0);
	}

	~ProtectedNamedData()
	{
		::ReleaseMutex(mUnprotectedNamedData.mLocal->mLock);
	}

	T* operator->() {
		return mUnprotectedNamedData.mLocal;
	}

	const T* operator->() const {
		return mUnprotectedNamedData.mLocal;
	}
};

// �}���`�p�[�g�̕�����
#define SIMPLE_DOWNLOAD_THRESHOLD		(1024ULL * 1024 * 4)
//#define SIMPLE_DOWNLOAD_THRESHOLD		(1024ULL)

//
// openFIle() ���Ă΂ꂽ�Ƃ��� UParam �Ƃ��� PTFS_FILE_CONTEXT �ɕۑ�����������
// closeFile() �ō폜�����
//

struct ReadFileContext
{
	WINCSE_DEVICE_STATS& mStats;
	const std::wstring mBucket;
	const std::wstring mKey;
	const UINT32 mCreateOptions;
	const UINT32 mGrantedAccess;
	FSP_FSCTL_FILE_INFO mFileInfo;

	std::wstring mGuardString;

	HANDLE mFile = INVALID_HANDLE_VALUE;
	UINT64 mLastOffset = 0ULL;

	ReadFileContext(
		WINCSE_DEVICE_STATS& argStats,
		const std::wstring& argBucket, const std::wstring& argKey,
		const UINT32 argCreateOptions, const UINT32 argGrantedAccess,
		const FSP_FSCTL_FILE_INFO& argFileInfo)
		:
		mStats(argStats),
		mBucket(argBucket), mKey(argKey), mCreateOptions(argCreateOptions),
		mGrantedAccess(argGrantedAccess), mFileInfo(argFileInfo)
	{
		mGuardString = mBucket + L'/' + mKey;
	}

	std::wstring getGuardString() const
	{
		return mGuardString;
	}

	~ReadFileContext();
};

struct FilePart
{
	WINCSE_DEVICE_STATS& mStats;
	const UINT64 mOffset;
	const ULONG mLength;

	HANDLE mDone = NULL;
	bool mResult = false;

	std::atomic<bool> mInterrupt = false;

	FilePart(WINCSE_DEVICE_STATS& argStats, UINT64 argOffset, ULONG argLength);

	void SetResult(bool argResult)
	{
		mResult = argResult;
		const auto b = ::SetEvent(mDone);					// �V�O�i����Ԃɐݒ�
		APP_ASSERT(b);
	}

	~FilePart();
};

struct Shared_Simple : public SharedBase { };
struct Shared_Multipart : public SharedBase { };

// �e���v���[�g�֐��̎��̉����K�v
template Shared_Simple* getUnprotectedNamedDataByName<Shared_Simple>(const std::wstring& name);
template Shared_Multipart* getUnprotectedNamedDataByName<Shared_Multipart>(const std::wstring& name);


// EOF