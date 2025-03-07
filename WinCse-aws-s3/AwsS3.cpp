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

//
// ClientPtr
//
Aws::S3::S3Client* ClientPtr::operator->() noexcept
{
    mRefCount++;

    return std::unique_ptr<Aws::S3::S3Client>::operator->();
}

// ------------------------------------------------
// global
//

// malloc, calloc で確保したメモリを shared_ptr で解放するための関数
template <typename T>
void free_deleter(T* ptr)
{
    free(ptr);
}

// ファイル名から FSP_FSCTL_DIR_INFO のヒープ領域を生成し、いくつかのメンバを設定して返却
DirInfoType mallocDirInfoW(const std::wstring& argKey, const std::wstring& argBucket)
{
    APP_ASSERT(!argKey.empty());

    const auto keyLen = argKey.length();
    const auto keyLenBytes = keyLen * sizeof(WCHAR);
    const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
    const auto dirInfoSize = offFileNameBuf + keyLenBytes;
    const auto allocSize = dirInfoSize + sizeof(WCHAR);

    FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)calloc(1, allocSize);
    APP_ASSERT(dirInfo);

    dirInfo->Size = (UINT16)dirInfoSize;
    dirInfo->FileInfo.IndexNumber = HashString(argBucket + L'/' + argKey);

    //
    // 実行時にエラーとなる (Buffer is too small)
    // 
    // おそらく、FSP_FSCTL_DIR_INFO.FileNameBuf は [] として定義されているため
    // wcscpy_s では 0 byte 領域へのバッファ・オーバーフローとして認識されて
    // しまうのではないかと思う
    // 
    //wcscpy_s(dirInfo->FileNameBuf, wkeyLen, wkey.c_str());

    memcpy(dirInfo->FileNameBuf, argKey.c_str(), keyLenBytes);

    return DirInfoType(dirInfo, free_deleter<FSP_FSCTL_DIR_INFO>);
}

DirInfoType mallocDirInfoA(const std::string& argKey, const std::string& argBucket)
{
    return mallocDirInfoW(MB2WC(argKey), MB2WC(argBucket));
}

DirInfoType AwsS3::mallocDirInfoW_dir(
    const std::wstring& argKey, const std::wstring& argBucket, const UINT64 argFileTime)
{
    auto dirInfo = mallocDirInfoW(argKey, argBucket);
    APP_ASSERT(dirInfo);

    UINT32 FileAttributes = FILE_ATTRIBUTE_DIRECTORY | mDefaultFileAttributes;

    if (argKey != L"." && argKey != L".." && argKey[0] == L'.')
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

// EOF
