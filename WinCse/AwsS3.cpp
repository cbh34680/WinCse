#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <filesystem>


//
// AwsS3
//
AwsS3::AwsS3(const wchar_t* tmpdir, const wchar_t* iniSection,
    IWorker* delayedWorker, IWorker* idleWorker) :
    mTempDir(tmpdir), mIniSection(iniSection),
    mDelayedWorker(delayedWorker), mIdleWorker(idleWorker),
    mWorkDirTime(0), mMaxBuckets(-1), mMaxObjects(-1)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(std::filesystem::exists(tmpdir));
    APP_ASSERT(std::filesystem::is_directory(tmpdir));
    APP_ASSERT(iniSection);
}

AwsS3::~AwsS3()
{
    NEW_LOG_BLOCK();
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

    return std::shared_ptr<Aws::S3::S3Client>::operator->();
}

//
// global
//

// ファイル名から FSP_FSCTL_DIR_INFO のヒープ領域を生成し、いくつかのメンバを設定して返却
std::shared_ptr<FSP_FSCTL_DIR_INFO> mallocDirInfoW(const std::wstring& key, const std::wstring& bucket)
{
    APP_ASSERT(!key.empty());

    const auto keyLen = key.length();
    const auto keyLenBytes = keyLen * sizeof(WCHAR);
    const auto offFileNameBuf = FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf);
    const auto dirInfoSize = offFileNameBuf + keyLenBytes;
    const auto allocSize = dirInfoSize + sizeof(WCHAR);

    FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)calloc(1, allocSize);
    APP_ASSERT(dirInfo);

    dirInfo->Size = (UINT16)dirInfoSize;
    dirInfo->FileInfo.IndexNumber = HashString(bucket + L'/' + key);

    //
    // 実行時にエラーとなる (Buffer is too small)
    // 
    // おそらく、FSP_FSCTL_DIR_INFO.FileNameBuf は [] として定義されているため
    // wcscpy_s では 0 byte 領域へのバッファ・オーバーフローとして認識されて
    // しまうのではないかと思う
    // 
    //wcscpy_s(dirInfo->FileNameBuf, wkeyLen, wkey.c_str());

    memmove(dirInfo->FileNameBuf, key.c_str(), keyLenBytes);

    return std::shared_ptr<FSP_FSCTL_DIR_INFO>(dirInfo, free_deleter<FSP_FSCTL_DIR_INFO>);
}

std::shared_ptr<FSP_FSCTL_DIR_INFO> mallocDirInfoA(const std::string& key, const std::string& bucket)
{
    return mallocDirInfoW(MB2WC(key), MB2WC(bucket));
}

const char* AWS_DEFAULT_REGION = Aws::Region::US_EAST_1;

// EOF
