#include "AwsS3.hpp"
#include <filesystem>

using namespace WinCseLib;

//
// AwsS3
//
WinCseLib::ICSDevice* NewCSDevice(
    const wchar_t* argTempDir, const wchar_t* argIniSection,
    WinCseLib::IWorker* argDelayedWorker, WinCseLib::IWorker* argIdleWorker)
{
    return new AwsS3(argTempDir, argIniSection, argDelayedWorker, argIdleWorker);
}

AwsS3::AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection,
    IWorker* argDelayedWorker, IWorker* argIdleWorker) :
    mTempDir(argTempDir), mIniSection(argIniSection),
    mDelayedWorker(argDelayedWorker), mIdleWorker(argIdleWorker)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(std::filesystem::exists(argTempDir));
    APP_ASSERT(std::filesystem::is_directory(argTempDir));
}

AwsS3::~AwsS3()
{
    NEW_LOG_BLOCK();

    this->OnSvcStop();
}

bool AwsS3::isInBucketFiltersW(const std::wstring& arg)
{
    if (mBucketFilters.empty())
    {
        return true;
    }

    const auto it = std::find_if(mBucketFilters.begin(), mBucketFilters.end(), [&arg](const auto& re)
    {
        return std::regex_match(arg, re);
    });

    return it != mBucketFilters.end();
}

bool AwsS3::isInBucketFiltersA(const std::string& arg)
{
    return isInBucketFiltersW(MB2WC(arg));
}

DirInfoType AwsS3::makeDirInfo_dir(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime)
{
    auto dirInfo = makeDirInfo(argObjKey);
    APP_ASSERT(dirInfo);

    UINT32 FileAttributes = FILE_ATTRIBUTE_DIRECTORY | mDefaultFileAttributes;

    if (argObjKey.key() != L"." && argObjKey.key() != L".." && argObjKey.key()[0] == L'.')
    {
        // ‰B‚µƒtƒ@ƒCƒ‹
        FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    dirInfo->FileInfo.FileAttributes = FileAttributes;

    dirInfo->FileInfo.CreationTime = argFileTime;
    dirInfo->FileInfo.LastAccessTime = argFileTime;
    dirInfo->FileInfo.LastWriteTime = argFileTime;
    dirInfo->FileInfo.ChangeTime = argFileTime;

    return dirInfo;
}

//
// ClientPtr
//
Aws::S3::S3Client* ClientPtr::operator->() noexcept
{
    mRefCount++;

    return std::unique_ptr<Aws::S3::S3Client>::operator->();
}

// EOF
