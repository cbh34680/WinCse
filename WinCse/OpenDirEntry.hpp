#pragma once

#include "CSDriverInternal.h"

// マクロにする必要性はないが、わかりやすいので

#if defined(THREAD_SAFE)
#error "THREAD_SAFFE(): already defined"
#endif

#define THREAD_SAFE()       std::lock_guard<std::mutex> lock_{ mGuard }

namespace CSEDRV
{

class OpenDirEntry final
{
	struct DirEntryWithRefCount
	{
		DirEntryWithRefCount(const CSELIB::DirEntryType& argDirEntry)
			:
			mDirEntry(argDirEntry)
		{
		}

		CSELIB::DirEntryType mDirEntry;
		int mRefCount = 0;
	};

	std::map<std::filesystem::path, DirEntryWithRefCount> mMap;
	mutable std::mutex mGuard;

public:
	bool addAndAcquire(const std::filesystem::path& argFileName, const CSELIB::DirEntryType& argDirEntry)
	{
		THREAD_SAFE();

		const auto it = mMap.insert({ argFileName, argDirEntry });
		if (!it.second)
		{
			return false;
		}

		it.first->second.mRefCount++;

		return true;
	}

	CSELIB::DirEntryType acquire(const std::filesystem::path& argFileName, bool addRefCount=true)
	{
		THREAD_SAFE();

		const auto it{ mMap.find(argFileName) };
		if (it == mMap.cend())
		{
			return nullptr;
		}

		if (addRefCount)
		{
			it->second.mRefCount++;
		}

		return it->second.mDirEntry;
	}

	bool release(const std::filesystem::path& argFileName)
	{
		THREAD_SAFE();

		const auto it{ mMap.find(argFileName) };
		if (it == mMap.cend())
		{
			return false;
		}

		it->second.mRefCount--;

		if (it->second.mRefCount > 0)
		{
			return true;
		}

		return mMap.erase(argFileName) == 1;
	}

	// 以下は参照カウンタを操作しないもの

	CSELIB::DirEntryType get(const std::filesystem::path& argFileName)
	{
		return acquire(argFileName, false);
	}

	using copy_type = std::map<std::filesystem::path, CSELIB::DirEntryType>;

	copy_type copy_if(std::function<bool(const copy_type::value_type&)> callback) const
	{
		THREAD_SAFE();

		copy_type ret;

		for (const auto& it: mMap)
		{
			const copy_type::value_type value{ it.first, it.second.mDirEntry };

			if (callback(value))
			{
				ret.insert(value);
			}
		}

		return ret;
	}
};

}	// namespace CSELIB

#undef THREAD_SAFE

// EOF