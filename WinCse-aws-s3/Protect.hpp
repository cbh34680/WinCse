#pragma once

#include <mutex>

//
// ñºëOÇ≈ÇÃîrëºêßå‰
//
template<typename T> class UnprotectedShare;
template<typename T> class ProtectedShare;

class SharedBase
{
	std::mutex mMutex;
	int mRefCount = 0;

	template<typename T> friend class UnprotectedShare;
	template<typename T> friend class ProtectedShare;
};

template<typename T>
struct ShareStore
{
	std::mutex mMapGuard;
	std::unordered_map<std::wstring, std::unique_ptr<T>> mMap;
};

template<typename T>
class ProtectedShare
{
	T* mV;

	template<typename T> friend class UnprotectedShare;

	ProtectedShare(T* argV) : mV(argV)
	{
		mV->mMutex.lock();
	}

public:
	~ProtectedShare()
	{
		unlock();
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

	T* operator->() {
		return mV;
	}

	const T* operator->() const {
		return mV;
	}
};

template<typename T>
class UnprotectedShare
{
	ShareStore<T>* mStore;
	const std::wstring mName;
	T* mV = nullptr;

public:
	template<typename... Args>
	UnprotectedShare(ShareStore<T>* argStore, const std::wstring& argName, Args... args)
		: mStore(argStore), mName(argName)
	{
		std::lock_guard<std::mutex> _(mStore->mMapGuard);

		auto it{ mStore->mMap.find(mName) };
		if (it == mStore->mMap.end())
		{
			it = mStore->mMap.emplace(mName, std::make_unique<T>(args...)).first;
		}

		it->second->mRefCount++;

		static_assert(std::is_base_of<SharedBase, T>::value, "T must be derived from SharedBase");

		mV = dynamic_cast<T*>(it->second.get());
		_ASSERT(mV);
	}

	~UnprotectedShare()
	{
		std::lock_guard<std::mutex> _(mStore->mMapGuard);

		auto it{ mStore->mMap.find(mName) };

		it->second->mRefCount--;

		if (it->second->mRefCount == 0)
		{
			mStore->mMap.erase(it);
		}
	}

	ProtectedShare<T> lock()
	{
		return ProtectedShare<T>(this->mV);
	}
};

// EOF