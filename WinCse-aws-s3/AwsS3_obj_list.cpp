#include "AwsS3.hpp"
#include "ObjectCache.hpp"


using namespace WinCseLib;

// -----------------------------------------------------------------------------------
//
// �L���b�V�����܂߂�����������u���b�N
//
static ObjectCache gObjectCache;

int AwsS3::unlockDeleteCacheByObjKey(CALLER_ARG const WinCseLib::ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());

    return gObjectCache.deleteByObjKey(CONT_CALLER argObjKey);
}

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

//
// �\���p�̃L���b�V�� (Purpose::Display) �̒�����A�����ɍ��v����
// �t�@�C���̏����擾����
//
DirInfoType AwsS3::unlockFindInParentOfDisplay(CALLER_ARG const ObjectKey& argObjKey)
{
    StatsIncr(_unlockFindInParentOfDisplay);

    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());
    APP_ASSERT(argObjKey.hasKey());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    std::wstring parentDir;
    std::wstring filename;

    if (!SplitPath(argObjKey.key(), &parentDir, &filename))
    {
        traceW(L"fault: SplitPath");
        return nullptr;
    }

    traceW(L"parentDir=[%s] filename=[%s]", parentDir.c_str(), filename.c_str());

    // Purpose::Display �Ƃ��ĕۑ����ꂽ�L���b�V�����擾

    DirInfoListType dirInfoList;

    const bool inCache = gObjectCache.getPositive(CONT_CALLER
        ObjectKey{ argObjKey.bucket(), parentDir }, Purpose::Display, &dirInfoList);

    if (!inCache)
    {
        // �q���̃I�u�W�F�N�g��T���Ƃ��ɂ́A�e�f�B���N�g���̓L���b�V���ɑ��݂���͂�
        // �Ȃ̂ŁA��{�I�ɂ͒ʉ߂��Ȃ��͂�

        traceW(L"not found in positive-cache, check it");
        return nullptr;
    }

    const auto it = std::find_if(dirInfoList.begin(), dirInfoList.end(), [&filename](const auto& dirInfo)
    {
        std::wstring name{ dirInfo->FileNameBuf };

        if (name == L"." || name == L"..")
        {
            return false;
        }

        if (FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
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

        traceW(L"not found in parent-dir");
        return nullptr;
    }

    return *it;
}

// -----------------------------------------------------------------------------------
//
// �O��IF �� Purpose ���L�q�����Ȃ����߂̃u���b�N
// (�Ӗ���������ɂ����Ȃ�̂�)
//

bool AwsS3::unlockListObjects_Display(CALLER_ARG
    const WinCseLib::ObjectKey& argObjKey, DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(_unlockListObjects_Display);
    APP_ASSERT(argObjKey.valid());

    return this->unlockListObjects(CONT_CALLER argObjKey, Purpose::Display, pDirInfoList);
}

bool AwsS3::unlockHeadObject_File(CALLER_ARG
    const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(_unlockHeadObject_File);
    APP_ASSERT(argObjKey.meansFile());
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    if (this->unlockHeadObject(CONT_CALLER argObjKey, pFileInfo))
    {
        traceW(L"unlockHeadObject: found");

        return true;
    }

    traceW(L"unlockHeadObject: not found");

    // �e�f�B���N�g�����璲�ׂ�

    const auto dirInfo{ unlockFindInParentOfDisplay(CONT_CALLER argObjKey) };
    if (dirInfo)
    {
        traceW(L"unlockFindInParentOfDisplay: found");

        if (pFileInfo)
        {
            *pFileInfo = dirInfo->FileInfo;
        }

        return true;
    }

    traceW(L"unlockFindInParentOfDisplay: not found");

    return false;
}

DirInfoType AwsS3::unlockListObjects_Dir(CALLER_ARG const ObjectKey& argObjKey)
{
    StatsIncr(_unlockListObjects_Dir);
    APP_ASSERT(argObjKey.meansDir());
    NEW_LOG_BLOCK();

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

        return makeDirInfo_dir(argObjKey, (*dirInfoList.begin())->FileInfo.LastWriteTime);
    }

    traceW(L"unlockListObjects: not found");

    // �e�f�B���N�g�����璲�ׂ�

    return this->unlockFindInParentOfDisplay(CONT_CALLER argObjKey);
}

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

// ���|�[�g�̐���
void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    gObjectCache.report(CONT_CALLER fp);
}

// �Â��L���b�V���̍폜
void AwsS3::deleteOldObjects(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    gObjectCache.deleteOldRecords(CONT_CALLER threshold);
}

//
// �������牺�̃��\�b�h�� ShareStore �ɂ��C�����K�v
//
struct Shared : public SharedBase { };
static ShareStore<Shared> gSharedStore;


bool AwsS3::headObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject);
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());

    UnprotectedShare<Shared> unsafeShare(&gSharedStore, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                         // ���O�̃��b�N

        bool ret = false;

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
                traceW(L"fault: unlockListObjects");
            }
        }
        else
        {
            // �t�@�C���̑��݊m�F

            if (this->unlockHeadObject_File(CONT_CALLER argObjKey, pFileInfo))
            {
                ret = true;
            }
            else
            {
                traceW(L"fault: unlockHeadObject");
                return false;
            }
        }

        return ret;
                                                                            // ���O�̃��b�N������
    }                                                                       // ���O�ւ̎Q�Ƃ����
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(listObjects);
    APP_ASSERT(argObjKey.valid());

    UnprotectedShare<Shared> unsafeShare(&gSharedStore, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                         // ���O�̃��b�N

        return this->unlockListObjects_Display(CONT_CALLER argObjKey, pDirInfoList);

                                                                            // ���O�̃��b�N������
    }                                                                       // ���O�ւ̎Q�Ƃ����
}

//
// �ȍ~�� override �ł͂Ȃ�����
//

int AwsS3::deleteCacheByObjKey(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(argObjKey.valid());

    UnprotectedShare<Shared> unsafeShare(&gSharedStore, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                         // ���O�̃��b�N

        return this->unlockDeleteCacheByObjKey(CONT_CALLER argObjKey);

                                                                            // ���O�̃��b�N������
    }                                                                       // ���O�ւ̎Q�Ƃ����
}

// EOF