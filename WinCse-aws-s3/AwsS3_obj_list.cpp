#include "AwsS3.hpp"

using namespace WinCseLib;

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool AwsS3::headObject_File(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_File);
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // �t�@�C���̑��݊m�F

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    if (!this->unsafeHeadObjectWithCache(CONT_CALLER argObjKey, pFileInfo))
    {
        traceW(L"not found: unsafeHeadObjectWithCache, argObjKey=%s", argObjKey.c_str());
        return false;
    }

    return true;
}

bool AwsS3::headObject_Dir(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_Dir);
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // �f�B���N�g���̑��݊m�F

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂�
    // �{���͊O������ listObjects() �����s���āA���W�b�N�Ŕ��f���邪
    // �Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    DirInfoListType dirInfoList;

    if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::CheckDirExists, &dirInfoList))
    {
        traceW(L"not found: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());
        return false;
    }

    APP_ASSERT(dirInfoList.size() == 1);

    // �f�B���N�g���̏ꍇ�� FSP_FSCTL_FILE_INFO �ɓK���Ȓl�𖄂߂�
    // ... �擾�����v�f�̏��([0]) ���t�@�C���̏ꍇ������̂ŁA�ҏW���K�v

    const auto dirInfo{ makeDirInfo_dir(argObjKey.key(), (*dirInfoList.begin())->FileInfo.LastWriteTime) };

    if (pFileInfo)
    {
        *pFileInfo = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    StatsIncr(listObjects);
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    DirInfoListType dirInfoList;

    if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::Display, &dirInfoList))
    {
        traceW(L"fault: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());
        return false;
    }

    if (pDirInfoList)
    {
        for (auto& dirInfo: dirInfoList)
        {
            if (!FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
            {
                const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

                if (mConfig.strictFileTimestamp)
                {
                    // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject ���擾

                    FSP_FSCTL_FILE_INFO mergeFileInfo;

                    if (this->unsafeHeadObjectWithCache(CONT_CALLER fileObjKey, &mergeFileInfo))
                    {
                        dirInfo->FileInfo = mergeFileInfo;

                        //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                    }
                }
                else
                {
                    // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject �̃L���b�V��������

                    DirInfoType mergeDirInfo;

                    if (this->unsafeGetPositiveCache_File(CONT_CALLER fileObjKey, &mergeDirInfo))
                    {
                        // HeadObject �̌��ʂ���ꂽ�Ƃ�
                        //
                        // --> ���^���ɍX�V�����Ȃǂ��L�^����Ă���̂ŁAxcopy �̂悤��
                        //     �t�@�C���������������삪�s����͂�
                        // 
                        // --> *dirInfo = *mergeDirInfo �Ƃ����ق��������I�����A�ꉞ �������R�s�[

                        dirInfo->FileInfo = mergeDirInfo->FileInfo;

                        //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                    }
                }
            }
        }

        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

//
// �ȍ~�� override �ł͂Ȃ�����
//

// ���|�[�g�̐���
void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();
    APP_ASSERT(fp);

    this->unsafeReportObjectCache(CONT_CALLER fp);
}

// �Â��L���b�V���̍폜
int AwsS3::deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    return this->unsafeDeleteOldObjectCache(CONT_CALLER threshold);
}

int AwsS3::clearObjectCache(CALLER_ARG0)
{
    THREAD_SAFE();

    return this->unsafeClearObjectCache(CONT_CALLER0);
}

int AwsS3::deleteObjectCache(CALLER_ARG const ObjectKey& argObjKey)
{
    THREAD_SAFE();
    APP_ASSERT(argObjKey.valid());

    return this->unsafeDeleteObjectCache(CONT_CALLER argObjKey);
}

// EOF