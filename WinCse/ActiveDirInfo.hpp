#pragma once

#include "CSDriverCommon.h"

// マクロにする必要性はないが、わかりやすいので

#define THREAD_SAFE()       std::lock_guard<std::mutex> lock_{ mGuard }

namespace CSEDRV
{

class ActiveDirInfo final
{
	struct DirInfoWithRefCount
	{
		DirInfoWithRefCount(const CSELIB::DirInfoPtr& argDirInfo) noexcept
			:
			mDirInfo(argDirInfo)
		{
		}

		CSELIB::DirInfoPtr mDirInfo;
		int mRefCount = 0;
	};

	std::mutex mGuard;
	std::map<std::filesystem::path, DirInfoWithRefCount> mMap;

public:
	bool addAndAcquire(const std::filesystem::path& argFileName, const CSELIB::DirInfoPtr& argDirInfo) noexcept
	{
		THREAD_SAFE();

		const auto it = mMap.insert({ argFileName, argDirInfo });
		if (!it.second)
		{
			return false;
		}

		it.first->second.mRefCount++;

		return true;
	}

	CSELIB::DirInfoPtr acquire(const std::filesystem::path& argFileName, bool addRefCount=true) noexcept
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

		return it->second.mDirInfo;
	}

	bool release(const std::filesystem::path& argFileName) noexcept
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

	CSELIB::DirInfoPtr get(const std::filesystem::path& argFileName) noexcept
	{
		return acquire(argFileName, false);
	}

	std::map<std::filesystem::path, CSELIB::DirInfoPtr> copy() noexcept
	{
		THREAD_SAFE();

		std::map<std::filesystem::path, CSELIB::DirInfoPtr> ret;

		for (const auto& it: mMap)
		{
			ret.insert({ it.first, it.second.mDirInfo });
		}

		return ret;
	}

};

}	// namespace CSELIB

#undef THREAD_SAFE

// EOF