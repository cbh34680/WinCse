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

DirInfoType AwsS3::makeDirInfo_dir(const WinCseLib::ObjectKey& argObjKey, const UINT64 argFileTime)
{
    auto dirInfo = makeDirInfo(argObjKey);
    APP_ASSERT(dirInfo);

    UINT32 FileAttributes = FILE_ATTRIBUTE_DIRECTORY | mDefaultFileAttributes;

    if (argObjKey.key() != L"." && argObjKey.key() != L".." && argObjKey.key()[0] == L'.')
    {
        // 隠しファイル
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

//
// FileOutputMeta
//
std::wstring FileOutputMeta::str() const
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
    ss << L" mSetFileTime=";
    ss << BOOL_CSTRW(mSetFileTime);

    return ss.str();
}

//
// OpenContext
//
bool OpenContext::openLocalFile(const DWORD argDesiredAccess, const DWORD argCreationDisposition)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(mLocalFile.invalid());

    // キャッシュ・ファイルを開き、HANDLE をコンテキストに保存

    ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;

    if (mCreateOptions & FILE_DELETE_ON_CLOSE)
    {
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
    }

    const DWORD dwDesiredAccess = mGrantedAccess | argDesiredAccess;

    mLocalFile = ::CreateFileW
    (
        getLocalPath().c_str(),
        dwDesiredAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        argCreationDisposition,
        CreateFlags,
        NULL
    );

    if (mLocalFile.invalid())
    {
        traceW(L"fault: CreateFileW");
        return false;
    }

    StatsIncr(_CreateFile);

    return true;
}

// EOF
