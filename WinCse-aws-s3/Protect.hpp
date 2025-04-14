#pragma once

#include "WinCseLib.h"
#include <mutex>

//
// ñºëOÇ≈ÇÃîrëºêßå‰
//
template <typename T> class UnprotectedShare;
template <typename T> class ProtectedShare;

class SharedBase
{
	std::mutex mMutex;
	int mRefCount = 0;

	template<typename T> friend class UnprotectedShare;
	template<typename T> friend class ProtectedShare;
};

template <typename T>
struct ShareStore
{
	std::mutex mMapGuard;
	std::unordered_map<std::wstring, std::unique_ptr<T>> mMap;
};

template <typename T>
class ProtectedShare
{
	T* mV;

	template<typename T> friend class UnprotectedShare;

	ProtectedShare(T* argV) noexcept : mV(argV)
	{
		mV->mMutex.lock();
	}

	void unlock() noexcept
	{
		if (mV)
		{
#pragma warning(suppress: 26110)
			mV->mMutex.unlock();
			mV = nullptr;
		}
	}

public:
	~ProtectedShare()
	{
		unlock();
	}

	T* operator->() noexcept {
		return mV;
	}

	const T* operator->() const noexcept {
		return mV;
	}
};

template <typename T>
class UnprotectedShare
{
	ShareStore<T>* mShareStore;
	const std::wstring mName;
	T* mV = nullptr;

public:
	template <typename... ArgsT>
	UnprotectedShare(ShareStore<T>* argShareStore, const std::wstring& argName, ArgsT... args) noexcept
		:
		mShareStore(argShareStore),
		mName(argName)
	{
		std::lock_guard<std::mutex> lock_{ mShareStore->mMapGuard };

		auto it{ mShareStore->mMap.find(mName) };
		if (it == mShareStore->mMap.end())
		{
			it = mShareStore->mMap.emplace(mName, std::make_unique<T>(args...)).first;
		}

		it->second->mRefCount++;

		static_assert(std::is_base_of<SharedBase, T>::value, "T must be derived from SharedBase");

		//mV = dynamic_cast<T*>(it->second.get());
		mV = static_cast<T*>(it->second.get());
		_ASSERT(mV);
	}

	~UnprotectedShare()
	{
		std::lock_guard<std::mutex> lock_{ mShareStore->mMapGuard };

		auto it{ mShareStore->mMap.find(mName) };

		it->second->mRefCount--;

		if (it->second->mRefCount == 0)
		{
			mShareStore->mMap.erase(it);
		}
	}

	ProtectedShare<T> lock() const noexcept
	{
		return ProtectedShare<T>(this->mV);
	}
};

// EOF