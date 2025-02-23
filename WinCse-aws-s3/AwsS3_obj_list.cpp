#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <cinttypes>


using namespace WinCseLib;


//
// ListObjectsV2 API �����s�����ʂ������̃|�C���^�̎w���ϐ��ɕۑ�����
// �����̏����ɍ��v����I�u�W�F�N�g��������Ȃ��Ƃ��� false ��ԋp
//
bool AwsS3::unsafeListObjectsV2(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
    const int limit, const bool delimiter)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);

    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(WC2MB(argBucket).c_str());

    if (delimiter)
    {
        request.WithDelimiter("/");
    }

    const auto argKeyLen = argKey.length();
    if (argKeyLen > 0)
    {
        request.SetPrefix(WC2MB(argKey).c_str());
    }

    UINT64 commonPrefixTime = UINT64_MAX;
    std::set<std::wstring> already;

    Aws::String continuationToken;                              // Used for pagination.

    do
    {
        if (!continuationToken.empty())
        {
            request.SetContinuationToken(continuationToken);
        }

        const auto outcome = mClient.ptr->ListObjectsV2(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: ListObjectsV2");

            return false;
        }

        const auto& result = outcome.GetResult();

        //
        // �f�B���N�g���E�G���g���̂��ߍŏ��Ɉ�ԌÂ��^�C���X�^���v�����W
        // * CommonPrefix �ɂ̓^�C���X�^���v���Ȃ�����
        //
        for (const auto& obj : result.GetContents())
        {
            const auto lastModified = UtcMillisToWinFileTimeIn100ns(obj.GetLastModified().Millis());

            if (lastModified < commonPrefixTime)
            {
                commonPrefixTime = lastModified;
            }
        }

        if (commonPrefixTime == UINT64_MAX)
        {
            // �^�C���X�^���v���̎�ł��Ȃ���ΎQ�ƃf�B���N�g���̂��̂��̗p

            commonPrefixTime = mWorkDirTime;
        }

        // �f�B���N�g���̎��W
        for (const auto& obj : result.GetCommonPrefixes())
        {
            const std::string fullPath{ obj.GetPrefix().c_str() };      // Aws::String -> std::string

            traceA("GetCommonPrefixes: %s", fullPath.c_str());

            std::wstring key{ MB2WC(fullPath.substr(argKeyLen)) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                }
            }

            if (key.empty())
            {
                key = L".";
            }

            APP_ASSERT(!key.empty());

            if (std::find(already.begin(), already.end(), key) != already.end())
            {
                traceW(L"%s: already added", key.c_str());
                continue;
            }

            already.insert(key);

            auto dirInfo = mallocDirInfoW(key, argBucket);
            APP_ASSERT(dirInfo);

            UINT32 FileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;

            if (key != L"." && key != L".." && key[0] == L'.')
            {
                // �B���t�@�C��
                FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            dirInfo->FileInfo.FileAttributes = FileAttributes;

            dirInfo->FileInfo.CreationTime = commonPrefixTime;
            dirInfo->FileInfo.LastAccessTime = commonPrefixTime;
            dirInfo->FileInfo.LastWriteTime = commonPrefixTime;
            dirInfo->FileInfo.ChangeTime = commonPrefixTime;

            dirInfoList.push_back(dirInfo);

            if (limit > 0)
            {
                if (dirInfoList.size() >= limit)
                {
                    goto exit;
                }
            }

            if (mMaxObjects > 0)
            {
                if (dirInfoList.size() >= mMaxObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mMaxObjects);

                    goto exit;
                }
            }
        }

        // �t�@�C���̎��W
        for (const auto& obj : result.GetContents())
        {
            const std::string fullPath{ obj.GetKey().c_str() };     // Aws::String -> std::string

            traceA("GetContents: %s", fullPath.c_str());

            bool isDir = false;

            std::wstring key{ MB2WC(fullPath.substr(argKeyLen)) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    isDir = true;
                    key.pop_back();
                }
            }

            if (key.empty())
            {
                isDir = true;
                key = L".";
            }

            APP_ASSERT(!key.empty());

            if (std::find(already.begin(), already.end(), key) != already.end())
            {
                traceW(L"%s: already added", key.c_str());
                continue;
            }

            already.insert(key);

            auto dirInfo = mallocDirInfoW(key, argBucket);
            APP_ASSERT(dirInfo);

            UINT32 FileAttributes = FILE_ATTRIBUTE_READONLY;

            if (key != L"." && key != L".." && key[0] == L'.')
            {
                // �B���t�@�C��
                FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            if (isDir)
            {
                FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            else
            {
                dirInfo->FileInfo.FileSize = obj.GetSize();
                dirInfo->FileInfo.AllocationSize = (dirInfo->FileInfo.FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
            }

            dirInfo->FileInfo.FileAttributes |= FileAttributes;

            const auto lastModified = UtcMillisToWinFileTimeIn100ns(obj.GetLastModified().Millis());

            dirInfo->FileInfo.CreationTime = lastModified;
            dirInfo->FileInfo.LastAccessTime = lastModified;
            dirInfo->FileInfo.LastWriteTime = lastModified;
            dirInfo->FileInfo.ChangeTime = lastModified;

            dirInfoList.push_back(dirInfo);

            if (limit > 0)
            {
                if (dirInfoList.size() >= limit)
                {
                    // ���ʃ��X�g�������Ŏw�肵�� limit �ɓ��B

                    goto exit;
                }
            }

            if (mMaxObjects > 0)
            {
                if (dirInfoList.size() >= mMaxObjects)
                {
                    // ���ʃ��X�g�� ini �t�@�C���Ŏw�肵���ő�l�ɓ��B

                    traceW(L"warning: over max-objects(%d)", mMaxObjects);

                    goto exit;
                }
            }
        }

        continuationToken = result.GetNextContinuationToken();
    } while (!continuationToken.empty());

exit:
    *pDirInfoList = dirInfoList;

    return !dirInfoList.empty();
}

std::shared_ptr<FSP_FSCTL_DIR_INFO> AwsS3::unsafeHeadObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(WC2MB(argBucket).c_str());
    request.SetKey(WC2MB(argKey).c_str());

    const auto outcome = mClient.ptr->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject �̎��s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�

        return nullptr;
    }

    const auto& result = outcome.GetResult();

    auto dirInfo = mallocDirInfoW(argKey, argBucket);
    APP_ASSERT(dirInfo);

    const auto FileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTimeIn100ns(result.GetLastModified().Millis());

    UINT32 FileAttributes = FILE_ATTRIBUTE_READONLY;

    if (argKey != L"." && argKey != L".." && argKey[0] == L'.')
    {
        FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    dirInfo->FileInfo.FileAttributes = FileAttributes;
    dirInfo->FileInfo.FileSize = FileSize;
    dirInfo->FileInfo.AllocationSize = (FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
    dirInfo->FileInfo.CreationTime = lastModified;
    dirInfo->FileInfo.LastAccessTime = lastModified;
    dirInfo->FileInfo.LastWriteTime = lastModified;
    dirInfo->FileInfo.ChangeTime = lastModified;
    dirInfo->FileInfo.IndexNumber = HashString(argBucket + L'/' + argKey);

    return dirInfo;
}


static std::mutex gGuard;

#define THREAD_SAFE() \
    std::lock_guard<std::mutex> lock_(gGuard)


bool AwsS3::headObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKey.empty());
    APP_ASSERT(argKey.back() != L'/');

    traceW(L"bucket: %s, key: %s", argBucket.c_str(), argKey.c_str());

    std::shared_ptr<FSP_FSCTL_DIR_INFO> dirInfo;

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�
    {
        // ���� dirInfoList ���g���Ă���̂ŁA�u���b�N�ɓ���ĉ��

        std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

        if (mObjectCache.getPositive(CONT_CALLER argBucket, argKey, -1, false, &dirInfoList))
        {
            // -1 �Ō������Ă���̂ňꌏ�݂̂ł���͂�

            APP_ASSERT(dirInfoList.size() == 1);

            traceW(L"found in positive-cache");
            dirInfo = dirInfoList[0];
        }
    }

    if (!dirInfo)
    {
        traceW(L"not found in positive-cache");

        // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

        if (mObjectCache.isInNegative(CONT_CALLER argBucket, argKey, -1, false))
        {
            // �l�K�e�B�u�E�L���b�V���ɂ��� == �f�[�^�͑��݂��Ȃ�
            traceW(L"found in negative cache");

            return false;
        }

        // HeadObject API �̎��s
        traceW(L"do HeadObject");

        dirInfo = unsafeHeadObject(CONT_CALLER argBucket, argKey);
        if (!dirInfo)
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^
            traceW(L"add negative");

            mObjectCache.addNegative(CONT_CALLER argBucket, argKey, -1, false);

            return false;
        }

        // �L���b�V���ɃR�s�[
        {
            // �ꌏ�݂̂̃��X�g�ɂ��� (�L���b�V���̌`���ɍ��킹��)
            std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList{ dirInfo };

            mObjectCache.setPositive(CONT_CALLER argBucket, argKey, -1, false, dirInfoList);
        }
    }

    if (pFileInfo)
    {
        (*pFileInfo) = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
    const int limit, const bool delimiter)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(argBucket.back() != L'/');

    if (!argKey.empty())
    {
        APP_ASSERT(argKey.back() == L'/');
    }

    traceW(L"bucket: %s, key: %s, limit: %d", argBucket.c_str(), argKey.c_str(), limit);

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;
    const bool inCache = mObjectCache.getPositive(CONT_CALLER argBucket, argKey, limit, delimiter, &dirInfoList);

    if (inCache)
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
        traceW(L"found in positive-cache");
    }
    else
    {
        traceW(L"not found in positive-cache");

        if (mObjectCache.isInNegative(CONT_CALLER argBucket, argKey, limit, delimiter))
        {
            // �l�K�e�B�u�E�L���b�V�����Ɍ�������
            traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() �̎��s
        traceW(L"call doListObjectV2");

        if (!this->unsafeListObjectsV2(CONT_CALLER argBucket, argKey, &dirInfoList, limit, delimiter))
        {
            // ���s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�
            traceW(L"object not found");

            // �l�K�e�B�u�E�L���b�V���ɓo�^
            traceW(L"add negative");
            mObjectCache.addNegative(CONT_CALLER argBucket, argKey, limit, delimiter);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        mObjectCache.setPositive(CONT_CALLER argBucket, argKey, limit, delimiter, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// EOF