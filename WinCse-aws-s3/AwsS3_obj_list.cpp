#include "AwsS3.hpp"

using namespace WCSE;

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//
//static std::mutex gGuard;
//#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

struct ObjectListShare : public SharedBase { };
static ShareStore<ObjectListShare> gObjectListShare;


bool AwsS3::headObject_File(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_File);
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        // �t�@�C���̑��݊m�F

        // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
        // --> �X�V���ꂽ�Ƃ����l��

        if (!this->unsafeHeadObjectWithCache(CONT_CALLER argObjKey, pFileInfo))
        {
            traceW(L"not found: unsafeHeadObjectWithCache, argObjKey=%s", argObjKey.c_str());
            return false;
        }

        return true;

    }   // ���O�̃��b�N������ (safeShare �̐�������)
}

bool AwsS3::headObject_Dir(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_Dir);
    //THREAD_SAFE();
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // �f�B���N�g���̑��݊m�F

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂�
    // �{���͊O������ listObjects() �����s���āA���W�b�N�Ŕ��f���邪
    // �Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    // ���ړI�ȃL���b�V����D�悵�Ē��ׂ�
    // --> �X�V���ꂽ�Ƃ����l��

    DirInfoListType dirInfoList;

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::CheckDirExists, &dirInfoList))
        {
            //traceW(L"fault: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());

            return false;
        }

    }   // ���O�̃��b�N������ (safeShare �̐�������)

    APP_ASSERT(dirInfoList.size() == 1);

    // �f�B���N�g���̏ꍇ�� FSP_FSCTL_FILE_INFO �ɓK���Ȓl�𖄂߂�
    // ... �擾�����v�f�̏��([0]) ���t�@�C���̏ꍇ������̂ŁA�ҏW���K�v

    const auto it{ dirInfoList.begin() };
    const auto dirInfo{ makeDirInfo_dir(argObjKey.key(), (*it)->FileInfo.LastWriteTime) };

    if (pFileInfo)
    {
        //FSP_FSCTL_FILE_INFO fileInfo = *dirInfo->FileInfo;
        //*pFileInfo = fileInfo;

        *pFileInfo = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    StatsIncr(listObjects);
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    DirInfoListType dirInfoList;

    {
        UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
        {
            const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

            if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::Display, &dirInfoList))
            {
                traceW(L"fault: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());
                return false;
            }

        }   // ���O�̃��b�N������ (safeShare �̐�������)
    }

    if (pDirInfoList)
    {
        // �\���p�̃��X�g�� CMD �Ɠ��������������邽�� ".", ".." �����݂��Ȃ��ꍇ�ɒǉ�����
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
                dirInfoList.insert(dirInfoList.begin(), makeDirInfo_dir(L"..", mWorkDirCTime));
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
            dirInfoList.insert(dirInfoList.begin(), makeDirInfo_dir(L".", mWorkDirCTime));
        }
        else
        {
            const auto save{ *itCurr };
            dirInfoList.erase(itCurr);
            dirInfoList.insert(dirInfoList.begin(), save);
        }

        //
        for (auto& dirInfo: dirInfoList)
        {
            if (FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
            {
                // �f�B���N�g���͊֌W�Ȃ�

                continue;
            }

            const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

            UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, fileObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
            {
                const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

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

                if (this->unsafeIsInNegativeCache_File(CONT_CALLER fileObjKey))
                {
                    // ���[�W�����Ⴂ�Ȃǂ� HeadObject �����s�������̂� HIDDEN ������ǉ�

                    dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }

            }   // ���O�̃��b�N������ (safeShare �̐�������)
        }

        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// EOF