#include "AwsS3.hpp"
#include "ObjectCache.hpp"


using namespace WinCseLib;


// -----------------------------------------------------------------------------------
//
// �L���b�V�����܂߂�����������u���b�N
//
extern ObjectCache gObjectCache;

bool AwsS3::unsafeHeadObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    bool alsoSearchCache, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKey.empty());
    APP_ASSERT(argKey.back() != L'/');

    traceW(L"bucket: %s, key: %s", argBucket.c_str(), argKey.c_str());

    DirInfoType dirInfo;

    if (alsoSearchCache)
    {
        // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

        if (gObjectCache.getPositive_File(CONT_CALLER argBucket, argKey, &dirInfo))
        {
            APP_ASSERT(dirInfo);

            traceW(L"found in positive-cache");
        }
    }

    if (!dirInfo)
    {
        if (alsoSearchCache)
        {
            traceW(L"not found in positive-cache");

            // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

            if (gObjectCache.isInNegative_File(CONT_CALLER argBucket, argKey))
            {
                // �l�K�e�B�u�E�L���b�V���ɂ��� == �f�[�^�͑��݂��Ȃ�

                traceW(L"found in negative cache");

                return false;
            }
        }

        // HeadObject API �̎��s
        traceW(L"do HeadObject");

        dirInfo = this->apicallHeadObject(CONT_CALLER argBucket, argKey);
        if (!dirInfo)
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            traceW(L"add negative");

            gObjectCache.addNegative_File(CONT_CALLER argBucket, argKey);

            return false;
        }

        // �L���b�V���ɃR�s�[

        gObjectCache.setPositive_File(CONT_CALLER argBucket, argKey, dirInfo);
    }

    if (pFileInfo)
    {
        (*pFileInfo) = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::unsafeListObjects(CALLER_ARG const Purpose argPurpose,
    const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(argBucket.back() != L'/');

    if (!argKey.empty())
    {
        APP_ASSERT(argKey.back() == L'/');
    }

    traceW(L"purpose=%s, bucket=%s, key=%s",
        PurposeString(argPurpose), argBucket.c_str(), argKey.c_str());

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoListType dirInfoList;
    const bool inCache = gObjectCache.getPositive(CONT_CALLER argPurpose, argBucket, argKey, &dirInfoList);

    if (inCache)
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������

        traceW(L"found in positive-cache");
    }
    else
    {
        traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative(CONT_CALLER argPurpose, argBucket, argKey))
        {
            // �l�K�e�B�u�E�L���b�V�����Ɍ�������

            traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() �̎��s
        traceW(L"call doListObjectV2");

        if (!this->apicallListObjectsV2(CONT_CALLER argPurpose, argBucket, argKey, &dirInfoList))
        {
            // ���s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�

            traceW(L"object not found");

            // �l�K�e�B�u�E�L���b�V���ɓo�^

            traceW(L"add negative");
            gObjectCache.addNegative(CONT_CALLER argPurpose, argBucket, argKey);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        gObjectCache.setPositive(CONT_CALLER argPurpose, argBucket, argKey, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// -----------------------------------------------------------------------------------
//
// �O��IF ����Ăяo�����u���b�N
//

bool AwsS3::unsafeListObjects_Display(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    StatsIncr(_unsafeListObjects_Display);

    return this->unsafeListObjects(CONT_CALLER Purpose::Display, argBucket, argKey, pDirInfoList);
}

//
// �\���p�̃L���b�V�� (Purpose::Display) �̒�����A�����ɍ��v����
// �t�@�C���̏����擾����
//
DirInfoType AwsS3::findFileInParentDirectry(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    StatsIncr(_findFileInParentDirectry);

    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKey.empty());

    traceW(L"bucket=[%s] key=[%s]", argBucket.c_str(), argKey.c_str());

    // �L�[����e�f�B���N�g�����擾

    auto tokens{ SplitW(argKey, L'/', false) };
    APP_ASSERT(!tokens.empty());

    // ""                      ABORT
    // "dir"                   OK
    // "dir/"                  OK
    // "dir/key.txt"           OK
    // "dir/key.txt/"          OK
    // "dir/subdir/key.txt"    OK
    // "dir/subdir/key.txt/"   OK

    auto filename{ tokens.back() };
    APP_ASSERT(!filename.empty());
    tokens.pop_back();

    // �����Ώۂ̐e�f�B���N�g��

    auto parentDir{ JoinW(tokens, L'/', false) };
    if (parentDir.empty())
    {
        // �o�P�b�g�̃��[�g�E�f�B���N�g�����猟��

        // "" --> ""
    }
    else
    {
        // �T�u�f�B���N�g�����猟��

        // "dir"        --> "dir/"
        // "dir/subdir" --> "dir/subdir/"

        parentDir += L'/';
    }

    // �����Ώۂ̃t�@�C���� (�f�B���N�g����)

    if (argKey.back() == L'/')
    {
        // SplitW() �� "/" ��������Ă��܂��̂ŁAargKey �� "dir/" �� "dir/file.txt/"
        // ���w�肳��Ă���Ƃ��� filename �� "/" ��t�^

        filename += L'/';
    }

    traceW(L"parentDir=[%s] filename=[%s]", parentDir.c_str(), filename.c_str());

    // Purpose::Display �Ƃ��ĕۑ����ꂽ�L���b�V�����擾

    DirInfoListType dirInfoList;
    const bool inCache = gObjectCache.getPositive(CONT_CALLER Purpose::Display, argBucket, parentDir, &dirInfoList);

    if (!inCache)
    {
        // �q���̃I�u�W�F�N�g��T���Ƃ��ɂ́A�e�f�B���N�g���̓L���b�V���ɑ��݂���͂�
        // �Ȃ̂ŁA��{�I�ɂ͒ʉ߂��Ȃ��͂�

        traceW(L"not found in positive-cache, check it", argBucket.c_str(), parentDir.c_str());
        return nullptr;
    }

    const auto it = std::find_if(dirInfoList.begin(), dirInfoList.end(), [&filename](const auto& dirInfo)
    {
        std::wstring name{ dirInfo->FileNameBuf };

        if (name == L"." || name == L"..")
        {
            return false;
        }

        if (dirInfo->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // FSP_FSCTL_DIR_INFO �� FileNameBuf �ɂ̓f�B���N�g���ł����Ă�
            // "/" �ŏI�[���Ă��Ȃ��̂ŁA��r�̂��߂� "/" ��t�^����

            name += L'/';
        }

        return filename == name;
    });

    if (it == dirInfoList.end())
    {
        // DoGetSecurityByName �̓f�B���N�g�����瑶�݃`�F�b�N���n�߂�̂�
        // �t�@�C�����ɑ΂��� "dir/file.txt/" �̂悤�Ȍ������n�߂�
        // ������ʉ߂���̂́A���̏ꍇ�݂̂��Ǝv��

        traceW(L"not found in parent-dir", argBucket.c_str(), filename.c_str());
        return nullptr;
    }

    return *it;
}

bool AwsS3::unsafeHeadObject_File(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    StatsIncr(_unsafeHeadObject_File);

    NEW_LOG_BLOCK();

    traceW(L"bucket=%s key=%s", argBucket.c_str(), argKey.c_str());

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    if (this->unsafeHeadObject(CONT_CALLER argBucket, argKey, true, pFileInfo))
    {
        traceW(L"unsafeHeadObject: found");

        return true;
    }

    traceW(L"unsafeHeadObject: not found");

    // �e�f�B���N�g�����璲�ׂ�

    const auto dirInfo{ findFileInParentDirectry(CONT_CALLER argBucket, argKey) };
    if (dirInfo)
    {
        traceW(L"findFileInParentDirectry: found");

        if (pFileInfo)
        {
            *pFileInfo = dirInfo->FileInfo;
        }

        return true;
    }

    traceW(L"findFileInParentDirectry: not found");

    return false;
}

DirInfoType AwsS3::unsafeListObjects_Dir(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    StatsIncr(_unsafeListObjects_Dir);

    NEW_LOG_BLOCK();

    traceW(L"bucket=%s key=%s", argBucket.c_str(), argKey.c_str());

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    DirInfoListType dirInfoList;

    if (this->unsafeListObjects(CONT_CALLER Purpose::CheckDir, argBucket, argKey, &dirInfoList))
    {
        APP_ASSERT(dirInfoList.size() == 1);

        traceW(L"unsafeListObjects: found");

        // �f�B���N�g���̏ꍇ�� FSP_FSCTL_FILE_INFO �ɓK���Ȓl�𖄂߂�
        // ... �擾�����v�f�̏��([0]) ���t�@�C���̏ꍇ������̂ŁA�ҏW���K�v

        return mallocDirInfoW_dir(argKey, argBucket, (*dirInfoList.begin())->FileInfo.ChangeTime);
    }

    traceW(L"unsafeListObjects: not found");

    // �e�f�B���N�g�����璲�ׂ�

    return findFileInParentDirectry(CONT_CALLER argBucket, argKey);
}

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)
ObjectCache gObjectCache;


bool AwsS3::headObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject);

    THREAD_SAFE();

    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());

    bool ret = false;

    traceW(L"bucket: %s, key: %s", argBucket.c_str(), argKey.c_str());

    // �L�[�̍Ō�̕����� "/" �����邩�ǂ����Ńt�@�C��/�f�B���N�g���𔻒f
    //
    if (argKey.empty() || (!argKey.empty() && argKey.back() == L'/'))
    {
        // �f�B���N�g���̑��݊m�F

        const auto dirInfo{ this->unsafeListObjects_Dir(CONT_CALLER argBucket, argKey) };
        if (dirInfo)
        {
            if (pFileInfo)
            {
                *pFileInfo = dirInfo->FileInfo;
            }

            ret = true;
        }
        else
        {
            traceW(L"fault: unsafeListObjects");
        }
    }
    else
    {
        // �t�@�C���̑��݊m�F

        if (this->unsafeHeadObject_File(CONT_CALLER argBucket, argKey, pFileInfo))
        {
            ret = true;
        }
        else
        {
            traceW(L"fault: unsafeHeadObject");
            return false;
        }
    }

    return ret;
}

bool AwsS3::headObject_File_SkipCacheSearch(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    // shouldDownload() ���Ŏg�p���Ă���A�Ώۂ̓t�@�C���̂�
    // �L���b�V���͒T���� api �����s����

    THREAD_SAFE();

    return this->unsafeHeadObject(CONT_CALLER argBucket, argKey, false, pFileInfo);
}

bool AwsS3::listObjects(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    StatsIncr(listObjects);

    THREAD_SAFE();

    return this->unsafeListObjects_Display(CONT_CALLER argBucket, argKey, pDirInfoList);
}

// ���|�[�g�̐���
void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();

    gObjectCache.report(CONT_CALLER fp);
}

// �Â��L���b�V���̍폜
void AwsS3::deleteOldObjects(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    gObjectCache.deleteOldRecords(CONT_CALLER threshold);
}

// EOF