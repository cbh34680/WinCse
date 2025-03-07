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

// malloc, calloc �Ŋm�ۂ����������� shared_ptr �ŉ�����邽�߂̊֐�
template <typename T>
void free_deleter(T* ptr)
{
    free(ptr);
}

// �t�@�C�������� FSP_FSCTL_DIR_INFO �̃q�[�v�̈�𐶐����A�������̃����o��ݒ肵�ĕԋp
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
    // ���s���ɃG���[�ƂȂ� (Buffer is too small)
    // 
    // �����炭�AFSP_FSCTL_DIR_INFO.FileNameBuf �� [] �Ƃ��Ē�`����Ă��邽��
    // wcscpy_s �ł� 0 byte �̈�ւ̃o�b�t�@�E�I�[�o�[�t���[�Ƃ��ĔF�������
    // ���܂��̂ł͂Ȃ����Ǝv��
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
        // �B���t�@�C��
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
