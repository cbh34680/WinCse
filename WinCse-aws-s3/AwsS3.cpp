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
    APP_ASSERT(std::filesystem::exists(argTempDir));
    APP_ASSERT(std::filesystem::is_directory(argTempDir));

    mStats = &mStats_;
}

AwsS3::~AwsS3()
{
    NEW_LOG_BLOCK();

    this->OnSvcStop();

    // 必要ないが、デバッグ時のメモリ・リーク調査の邪魔になるので
    clearBuckets(START_CALLER0);
    clearObjects(START_CALLER0);

    mRefFile.close();
    mRefDir.close();
}

bool AwsS3::isInBucketFilters(const std::wstring& arg)
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

DirInfoType AwsS3::makeDirInfo_attr(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime, const UINT32 argFileAttributes)
{
    auto dirInfo = makeDirInfo(argObjKey);
    APP_ASSERT(dirInfo);

    UINT32 fileAttributes = argFileAttributes | mDefaultFileAttributes;

    if (argObjKey.meansHidden())
    {
        // 隠しファイル
        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    dirInfo->FileInfo.FileAttributes = fileAttributes;

    dirInfo->FileInfo.CreationTime = argFileTime;
    dirInfo->FileInfo.LastAccessTime = argFileTime;
    dirInfo->FileInfo.LastWriteTime = argFileTime;
    dirInfo->FileInfo.ChangeTime = argFileTime;

    return dirInfo;
}

DirInfoType AwsS3::makeDirInfo_byName(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime)
{
    APP_ASSERT(argObjKey.valid());

    return makeDirInfo_attr(argObjKey, argFileTime, argObjKey.meansFile() ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DIRECTORY);
}

DirInfoType AwsS3::makeDirInfo_dir(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime)
{
    return makeDirInfo_attr(argObjKey, argFileTime, FILE_ATTRIBUTE_DIRECTORY);
}

//
// FileOutputParams
//
std::wstring FileOutputParams::str() const
{
    std::wstring sCreationDisposition;

    switch (mCreationDisposition)
    {
        case CREATE_ALWAYS:     sCreationDisposition = L"CREATE_ALWAYS";     break;
        case CREATE_NEW:        sCreationDisposition = L"CREATE_NEW";        break;
        case OPEN_ALWAYS:       sCreationDisposition = L"OPEN_ALWAYS";       break;
        case OPEN_EXISTING:     sCreationDisposition = L"OPEN_EXISTING";     break;
        case TRUNCATE_EXISTING: sCreationDisposition = L"TRUNCATE_EXISTING"; break;
        default: APP_ASSERT(0);
    }

    std::wstringstream ss;

    ss << L"mPath=";
    ss << mPath;
    ss << L" mCreationDisposition=";
    ss << sCreationDisposition;
    ss << L" mOffset=";
    ss << mOffset;
    ss << L" mLength=";
    ss << mLength;
    ss << L" mSpecifyRange=";
    ss << BOOL_CSTRW(mSpecifyRange);

    return ss.str();
}

//
// OpenContext
//


// EOF
