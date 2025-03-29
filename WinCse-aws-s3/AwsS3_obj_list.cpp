#include "AwsS3.hpp"
#include "ObjectCache.hpp"


using namespace WinCseLib;

// -----------------------------------------------------------------------------------
//
// �L���b�V�����܂߂�����������u���b�N
//
static ObjectCache gObjectCache;

bool AwsS3::unlockHeadObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    DirInfoType dirInfo;

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    if (gObjectCache.getPositive_File(CONT_CALLER argObjKey, &dirInfo))
    {
        APP_ASSERT(dirInfo);

        traceW(L"found in positive-cache");
    }

    if (!dirInfo)
    {
        traceW(L"not found in positive-cache");

        // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

        if (gObjectCache.isInNegative_File(CONT_CALLER argObjKey))
        {
            // �l�K�e�B�u�E�L���b�V���ɂ��� == �f�[�^�͑��݂��Ȃ�

            traceW(L"found in negative cache");

            return false;
        }

        // HeadObject API �̎��s
        traceW(L"do HeadObject");

        dirInfo = this->apicallHeadObject(CONT_CALLER argObjKey);
        if (!dirInfo)
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            traceW(L"add negative");

            gObjectCache.addNegative_File(CONT_CALLER argObjKey);

            return false;
        }

        // �L���b�V���ɃR�s�[

        gObjectCache.setPositive_File(CONT_CALLER argObjKey, dirInfo);
    }

    if (pFileInfo)
    {
        (*pFileInfo) = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::unlockListObjects(CALLER_ARG const ObjectKey& argObjKey,
    const Purpose argPurpose, DirInfoListType* pDirInfoList /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"purpose=%s, argObjKey=%s", PurposeString(argPurpose), argObjKey.c_str());

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoListType dirInfoList;
    const bool inCache = gObjectCache.getPositive(CONT_CALLER argObjKey, argPurpose, &dirInfoList);

    if (inCache)
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������

        traceW(L"found in positive-cache");
    }
    else
    {
        traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative(CONT_CALLER argObjKey, argPurpose))
        {
            // �l�K�e�B�u�E�L���b�V�����Ɍ�������

            traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() �̎��s
        traceW(L"call doListObjectV2");

        if (!this->apicallListObjectsV2(CONT_CALLER argPurpose, argObjKey, &dirInfoList))
        {
            // ���s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�

            traceW(L"object not found");

            // �l�K�e�B�u�E�L���b�V���ɓo�^

            traceW(L"add negative");
            gObjectCache.addNegative(CONT_CALLER argObjKey, argPurpose);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        gObjectCache.setPositive(CONT_CALLER argObjKey, argPurpose, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

void AwsS3::unlockReportObjectCache(CALLER_ARG FILE* fp)
{
    gObjectCache.report(CONT_CALLER fp);
}

int AwsS3::unlockDeleteOldObjects(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    return gObjectCache.deleteOldRecords(CONT_CALLER threshold);
}

int AwsS3::unlockClearObjects(CALLER_ARG0)
{
    return gObjectCache.deleteOldRecords(CONT_CALLER std::chrono::system_clock::now());
}

int AwsS3::unlockDeleteCacheByObjectKey(CALLER_ARG const WinCseLib::ObjectKey& argObjKey)
{
    return gObjectCache.deleteByObjectKey(CONT_CALLER argObjKey);
}

// -----------------------------------------------------------------------------------
//
// �O��IF �� Purpose ���L�q�����Ȃ����߂̃u���b�N
// (�Ӗ���������ɂ����Ȃ�̂�)
//

bool AwsS3::unlockListObjects_Display(CALLER_ARG
    const WinCseLib::ObjectKey& argObjKey, DirInfoListType* pDirInfoList /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

#if 0
    if (!this->unlockListObjects(CONT_CALLER argObjKey, Purpose::Display, pDirInfoList))
    {
        traceW(L"fault: unlockListObjects");
        return false;
    }

#else
    DirInfoListType dirInfoList;

    if (!this->unlockListObjects(CONT_CALLER argObjKey, Purpose::Display, &dirInfoList))
    {
        traceW(L"fault: unlockListObjects");
        return false;
    }

    for (auto& dirInfo: dirInfoList)
    {
        if (!FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
        {
            // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject �̃L���b�V��������

            const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

            DirInfoType mergeDirInfo;

            if (gObjectCache.getPositive_File(CONT_CALLER fileObjKey, &mergeDirInfo))
            {
                // HeadObject �̌��ʂ���ꂽ�Ƃ�
                //
                // --> ���^���ɍX�V�����Ȃǂ��L�^����Ă���̂ŁAxcopy �̂悤��
                //     �t�@�C���������������삪�s����͂�
                // 
                // --> *dirInfo = *mergeDirInfo �Ƃ����ق��������I�����A�ꉞ �������R�s�[

                dirInfo->FileInfo = mergeDirInfo->FileInfo;
            }
        }
    }

    *pDirInfoList = std::move(dirInfoList);

#endif
    return true;
}

bool AwsS3::unlockHeadObject_File(CALLER_ARG
    const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    if (this->unlockHeadObject(CONT_CALLER argObjKey, pFileInfo))
    {
        traceW(L"unlockHeadObject: found");

        return true;
    }

    traceW(L"unlockHeadObject: not found");

    return false;
}

DirInfoType AwsS3::unlockListObjects_Dir(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    DirInfoListType dirInfoList;

    if (this->unlockListObjects(CONT_CALLER argObjKey, Purpose::CheckDirExists, &dirInfoList))
    {
        APP_ASSERT(dirInfoList.size() == 1);

        traceW(L"unlockListObjects: found");

        // �f�B���N�g���̏ꍇ�� FSP_FSCTL_FILE_INFO �ɓK���Ȓl�𖄂߂�
        // ... �擾�����v�f�̏��([0]) ���t�@�C���̏ꍇ������̂ŁA�ҏW���K�v

        return makeDirInfo_dir(argObjKey.key(), (*dirInfoList.begin())->FileInfo.LastWriteTime);
    }

    traceW(L"unlockListObjects: not found");

    return nullptr;
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

bool AwsS3::headObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject);
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.hasKey());

    traceW(L"ObjectKey=%s", argObjKey.c_str());

    // �L�[�̍Ō�̕����� "/" �����邩�ǂ����Ńt�@�C��/�f�B���N�g���𔻒f
    //
    if (argObjKey.meansDir())
    {
        // �f�B���N�g���̑��݊m�F

        // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂�
        // �{���͊O������ listObjects() �����s���āA���W�b�N�Ŕ��f���邪
        // �Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

        const auto dirInfo{ this->unlockListObjects_Dir(CONT_CALLER argObjKey) };
        if (!dirInfo)
        {
            traceW(L"fault: unlockListObjects");
            return false;
        }

        if (pFileInfo)
        {
            *pFileInfo = dirInfo->FileInfo;
        }
    }
    else
    {
        // �t�@�C���̑��݊m�F

        if (!this->unlockHeadObject_File(CONT_CALLER argObjKey, pFileInfo))
        {
            traceW(L"fault: unlockHeadObject");
            return false;
        }
    }

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(listObjects);
    THREAD_SAFE();
    APP_ASSERT(argObjKey.meansDir());

    return this->unlockListObjects_Display(CONT_CALLER argObjKey, pDirInfoList);
}

//
// �ȍ~�� override �ł͂Ȃ�����
//

// ���|�[�g�̐���
void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();
    APP_ASSERT(fp);

    this->unlockReportObjectCache(CONT_CALLER fp);
}

// �Â��L���b�V���̍폜
int AwsS3::deleteOldObjects(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    return this->unlockDeleteOldObjects(CONT_CALLER threshold);
}

int AwsS3::clearObjects(CALLER_ARG0)
{
    THREAD_SAFE();

    return this->unlockClearObjects(CONT_CALLER0);
}

int AwsS3::deleteCacheByObjectKey(CALLER_ARG const ObjectKey& argObjKey)
{
    THREAD_SAFE();
    APP_ASSERT(argObjKey.valid());

    return this->unlockDeleteCacheByObjectKey(CONT_CALLER argObjKey);
}

// EOF