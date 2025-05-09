#pragma once

#include "WinCseLib.h"

namespace CSELIB
{

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

public:
	virtual ~SharedBase() = default;
};

template <typename T>
struct ShareStore final
{
	std::mutex mMapGuard;
	std::map<std::wstring, std::unique_ptr<T>> mMap;
};

template <typename T>
class ProtectedShare final
{
	T* mV;

	template<typename T> friend class UnprotectedShare;

	ProtectedShare(T* argV)
		:
		mV(argV)
	{
		APP_ASSERT(mV);

		mV->mMutex.lock();
	}

	void unlock()
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

	T* operator->()
	{
		return mV;
	}

	const T* operator->() const
	{
		return mV;
	}
};

template <typename T>
class UnprotectedShare final
{
	ShareStore<T>* mShareStore;
	const std::wstring mName;
	T* mV = nullptr;

public:
	template <typename... ArgsT>
	UnprotectedShare(ShareStore<T>* argShareStore, const std::wstring& argName, ArgsT... args)
		:
		mShareStore(argShareStore),
		mName(argName)
	{
		APP_ASSERT(argShareStore);
		APP_ASSERT(!mName.empty());

		std::lock_guard<std::mutex> lock_{ mShareStore->mMapGuard };

		auto it{ mShareStore->mMap.find(mName) };
		if (it == mShareStore->mMap.cend())
		{
			it = mShareStore->mMap.emplace(mName, std::make_unique<T>(args...)).first;
		}

		it->second->mRefCount++;

		static_assert(std::is_base_of<SharedBase, T>::value, "T must be derived from SharedBase");

		//mV = dynamic_cast<T*>(it->second.get());
		mV = static_cast<T*>(it->second.get());
		APP_ASSERT(mV);
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

	ProtectedShare<T> lock() const
	{
		return ProtectedShare<T>(this->mV);
	}
};

}	// namespace CSELIB

// EOF