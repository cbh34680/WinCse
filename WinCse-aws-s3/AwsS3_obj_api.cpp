#include "AwsS3.hpp"


using namespace WinCseLib;


DirInfoType AwsS3::apicallHeadObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = mClient.ptr->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject �̎��s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�
        traceW(L"fault: HeadObject");

        return nullptr;
    }

    auto dirInfo = makeDirInfo(argObjKey);
    APP_ASSERT(dirInfo);

    const auto& result = outcome.GetResult();
    const auto fileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTime100ns(result.GetLastModified().Millis());

    UINT64 creationTime = lastModified;
    UINT64 lastAccessTime = lastModified;
    UINT64 lastWriteTime = lastModified;

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.end())
    {
        creationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-access-time") != metadata.end())
    {
        lastAccessTime = std::stoull(metadata.at("wincse-last-access-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.end())
    {
        lastWriteTime = std::stoull(metadata.at("wincse-last-write-time"));
    }

    UINT32 fileAttributes = mDefaultFileAttributes;

    if (argObjKey.meansHidden())
    {
        // �B���t�@�C��
        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    if (fileAttributes == 0)
    {
        fileAttributes = FILE_ATTRIBUTE_NORMAL;
    }

    dirInfo->FileInfo.FileAttributes = fileAttributes;
    dirInfo->FileInfo.FileSize = fileSize;
    dirInfo->FileInfo.AllocationSize = (fileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
    dirInfo->FileInfo.CreationTime = creationTime;
    dirInfo->FileInfo.LastAccessTime = lastAccessTime;
    dirInfo->FileInfo.LastWriteTime = lastWriteTime;
    dirInfo->FileInfo.ChangeTime = lastModified;

    //dirInfo->FileInfo.IndexNumber = HashString(argObjKey.bucket() + L'/' + argObjKey.key());
    dirInfo->FileInfo.IndexNumber = HashString(argObjKey.str());

    return dirInfo;
}

//
// ListObjectsV2 API �����s�����ʂ������̃|�C���^�̎w���ϐ��ɕۑ�����
// �����̏����ɍ��v����I�u�W�F�N�g��������Ȃ��Ƃ��� false ��ԋp
//
bool AwsS3::apicallListObjectsV2(CALLER_ARG const Purpose argPurpose,
    const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);
    APP_ASSERT(argObjKey.valid());

    bool delimiter = false;
    int limit = 0;

    switch (argPurpose)
    {
        case Purpose::CheckDirExists:
        {
            // �f�B���N�g���̑��݊m�F�ׂ̈ɂ����Ă΂��͂�

            APP_ASSERT(argObjKey.hasKey());
            APP_ASSERT(argObjKey.meansDir());

            limit = 1;

            break;
        }
        case Purpose::Display:
        {
            // DoReadDirectory() ����̂݌Ăяo�����͂�

            APP_ASSERT(argObjKey.meansDir());

            delimiter = true;

            break;
        }
        default:
        {
            APP_ASSERT(0);
        }
    }

    traceW(L"purpose=%s, argObjKey=%s, delimiter=%s, limit=%d",
        PurposeString(argPurpose), argObjKey.c_str(), BOOL_CSTRW(delimiter), limit);

    DirInfoListType dirInfoList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(argObjKey.bucketA());

    if (delimiter)
    {
        request.WithDelimiter("/");
    }

    if (limit > 0)
    {
        request.SetMaxKeys(limit);
    }

    const auto argKeyLen = argObjKey.key().length();
    if (argObjKey.hasKey())
    {
        request.SetPrefix(argObjKey.keyA());
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
        for (const auto& it : result.GetContents())
        {
            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            if (lastModified < commonPrefixTime)
            {
                commonPrefixTime = lastModified;
            }
        }

        if (commonPrefixTime == UINT64_MAX)
        {
            // �^�C���X�^���v���̎�ł��Ȃ���ΎQ�ƃf�B���N�g���̂��̂��̗p

            commonPrefixTime = mWorkDirCTime;
        }

        // �f�B���N�g���̎��W
        for (const auto& it : result.GetCommonPrefixes())
        {
            const auto fullPath{ MB2WC(it.GetPrefix()) };
            traceW(L"GetCommonPrefixes: %s", fullPath.c_str());

            // Prefix ��������菜��
            // 
            // "dir/"           --> ""
            // "dir/file1.txt"  --> "file1.txt"

            auto key{ fullPath.substr(argKeyLen) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                }
            }

            if (key.empty())
            {
                // �t�@�C��������("") �̂��̂̓f�B���N�g���E�I�u�W�F�N�g�Ƃ��Ĉ���

                key = L".";
            }

            APP_ASSERT(!key.empty());

            {
                // �啶���������𓯈ꎋ������ŁA�������O���������疳������
                // 
                // --> Windows �ł͓����f�B���N�g���Ƃ��Ĉ����邽��

                auto keyUpper{ ToUpper(key) };

                if (std::find(already.begin(), already.end(), keyUpper) != already.end())
                {
                    traceW(L"%s: already added", keyUpper.c_str());
                    continue;
                }

                already.insert(keyUpper);
            }

            dirInfoList.push_back(makeDirInfo_dir(ObjectKey{ argObjKey.bucket(), key }, commonPrefixTime));

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
        for (const auto& it : result.GetContents())
        {
            bool isDir = false;

            const auto fullPath{ MB2WC(it.GetKey()) };
            traceW(L"GetContents: %s", fullPath.c_str());

            // Prefix ��������菜��
            // 
            // "dir/"           --> ""
            // "dir/file1.txt"  --> "file1.txt"

            auto key{ fullPath.substr(argKeyLen) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                    isDir = true;
                }
            }

            if (key.empty())
            {
                // �t�@�C��������("") �̂��̂̓f�B���N�g���E�I�u�W�F�N�g�Ƃ��Ĉ���

                isDir = true;
                key = L".";
            }

            APP_ASSERT(!key.empty());

            {
                // �啶���������𓯈ꎋ������ŁA�������O���������疳������
                std::wstring keyUpper{ ToUpper(key) };

                if (std::find(already.begin(), already.end(), keyUpper) != already.end())
                {
                    traceW(L"%s: already added", keyUpper.c_str());
                    continue;
                }

                already.insert(keyUpper);
            }

            auto dirInfo = makeDirInfo(ObjectKey{ argObjKey.bucket(), key });
            APP_ASSERT(dirInfo);

            UINT32 FileAttributes = mDefaultFileAttributes;

            if (key != L"." && key != L".." && key[0] == L'.')
            {
                // ".", ".." �ȊO�Ő擪�� "." �Ŏn�܂��Ă�����͉̂B���t�@�C���̈���

                FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            if (isDir)
            {
                FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            else
            {
                dirInfo->FileInfo.FileSize = it.GetSize();
                dirInfo->FileInfo.AllocationSize = (dirInfo->FileInfo.FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
            }

            dirInfo->FileInfo.FileAttributes |= FileAttributes;

            if (dirInfo->FileInfo.FileAttributes == 0)
            {
                dirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }

            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            dirInfo->FileInfo.CreationTime = lastModified;
            dirInfo->FileInfo.LastAccessTime = lastModified;
            dirInfo->FileInfo.LastWriteTime = lastModified;
            dirInfo->FileInfo.ChangeTime = lastModified;

            dirInfoList.emplace_back(dirInfo);

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
    if (argPurpose == Purpose::Display && !dirInfoList.empty())
    {
        //
        // �\���p�̃��X�g�ł���A��ł͂Ȃ��̂� cmd �Ɠ��������������邽��
        // ".", ".." �����݂��Ȃ��ꍇ�ɒǉ�����
        //

        // "C:\WORK" �̂悤�Ƀh���C�u�����̃f�B���N�g���ł� ".." ���\������Ȃ�����ɍ��킹��

        if (argObjKey.hasKey())
        {
            const auto itParent = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
            {
                return wcscmp(dirInfo->FileNameBuf, L"..") == 0;
            });

            if (itParent == dirInfoList.end())
            {
                dirInfoList.insert(dirInfoList.begin(), makeDirInfo_dir(ObjectKey{ argObjKey.bucket(), L".." }, commonPrefixTime));
            }
            else
            {
                const auto save{ *itParent };
                dirInfoList.erase(itParent);
                dirInfoList.insert(dirInfoList.begin(), save);
            }
        }

        const auto itCurr = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
        {
            return wcscmp(dirInfo->FileNameBuf, L".") == 0;
        });

        if (itCurr == dirInfoList.end())
        {
            dirInfoList.insert(dirInfoList.begin(), makeDirInfo_dir(ObjectKey{ argObjKey.bucket(), L"." }, commonPrefixTime));
        }
        else
        {
            const auto save{ *itCurr };
            dirInfoList.erase(itCurr);
            dirInfoList.insert(dirInfoList.begin(), save);
        }

        // �f�B���N�g�� -> �t�@�C���̏��ɒǉ�����Ă���̂ŕ��ёւ��͕s�v
        // 
        //bool compareDirInfo(const DirInfoType& a, const DirInfoType& b);
        //std::sort(dirInfoList.begin(), dirInfoList.end(), compareDirInfo);
    }

    *pDirInfoList = dirInfoList;

    return !dirInfoList.empty();
}

/*
#define IS_FA_DIR(ptr)        (ptr->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)

bool compareDirInfo(const DirInfoType& a, const DirInfoType& b)
{
    if (IS_FA_DIR(a) && !IS_FA_DIR(b))
    {
        return true;
    }
    if (!IS_FA_DIR(a) && IS_FA_DIR(b))
    {
        return false;
    }
    return wcscmp(a->FileNameBuf, b->FileNameBuf) < 0;
}
*/

// EOF