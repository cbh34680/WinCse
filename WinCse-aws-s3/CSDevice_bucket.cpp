#include "CSDevice.hpp"

using namespace WCSE;


static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }


bool CSDevice::headBucket(CALLER_ARG const std::wstring& argBucketName, WCSE::DirInfoType* pDirInfo)
{
    THREAD_SAFE();

    return mQueryBucket->unsafeHeadBucket(CONT_CALLER argBucketName, pDirInfo);
}

bool CSDevice::listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList)
{
    THREAD_SAFE();

    return mQueryBucket->unsafeListBuckets(CONT_CALLER pDirInfoList, {});
}

bool CSDevice::reloadBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    return mQueryBucket->unsafeReload(CONT_CALLER threshold);
}

// EOF