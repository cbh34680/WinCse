#include "AwsS3.hpp"

using namespace WCSE;


static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }


bool AwsS3::headBucket(CALLER_ARG const std::wstring& argBucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headBucket);
    THREAD_SAFE();

    return this->unsafeHeadBucket(CONT_CALLER argBucketName, pFileInfo);
}

bool AwsS3::listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(listBuckets);
    THREAD_SAFE();

    return this->unsafeListBuckets(CONT_CALLER pDirInfoList, {});
}

bool AwsS3::reloadListBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    return this->unsafeReloadListBuckets(CONT_CALLER threshold);
}

// EOF