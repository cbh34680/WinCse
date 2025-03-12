#pragma once

#include <atomic>

// AwsS3_obj_read.cpp, AwsS3_obj_r_single,cpp, AwsS3_obj_r_multiple.cpp
// からのみ include されること

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

// マルチパートの分割数
#define SIMPLE_DOWNLOAD_THRESHOLD		(1024ULL * 1024 * 4)
//#define SIMPLE_DOWNLOAD_THRESHOLD		(1024ULL)

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
		const auto b = ::SetEvent(mDone);					// シグナル状態に設定
		APP_ASSERT(b);
	}

	~FilePart();
};

struct Shared_Simple : public SharedBase { };
struct Shared_Multipart : public SharedBase { };

// テンプレート関数の実体化が必要
template Shared_Simple* getUnprotectedNamedDataByName<Shared_Simple>(const std::wstring& name);
template Shared_Multipart* getUnprotectedNamedDataByName<Shared_Multipart>(const std::wstring& name);


// EOF