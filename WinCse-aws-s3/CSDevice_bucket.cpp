#include "CSDevice.hpp"

using namespace WCSE;


static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }


DirInfoType CSDevice::headBucket(CALLER_ARG const std::wstring& argBucketName)
{
    THREAD_SAFE();

    return mQueryBucket->unsafeHeadBucket(CONT_CALLER argBucketName);
}

bool CSDevice::listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */)
{
    THREAD_SAFE();

    return mQueryBucket->unsafeListBuckets(CONT_CALLER pDirInfoList, {});
}

bool CSDevice::reloadBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    return mQueryBucket->unsafeReloadListBuckets(CONT_CALLER threshold);
}

// EOF