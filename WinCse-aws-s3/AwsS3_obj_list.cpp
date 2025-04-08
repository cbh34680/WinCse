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
//#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }

struct ObjectListShare : public SharedBase { };
static ShareStore<ObjectListShare> gObjectListShare;

bool AwsS3::headObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    //THREAD_SAFE();
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());
    APP_ASSERT(!argObjKey.isBucket());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // �f�B���N�g���̑��݊m�F

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂�
    // �{���͊O������ listObjects() �����s���āA���W�b�N�Ŕ��f���邪
    // �Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    DirInfoListType dirInfoList;

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        DirInfoType dirInfo;

        if (argObjKey.meansDir())
        {
            dirInfo = this->unsafeHeadObjectWithCache_CheckDir(CONT_CALLER argObjKey);
        }
        else
        {
            APP_ASSERT(argObjKey.meansFile());

            dirInfo = this->unsafeHeadObjectWithCache(CONT_CALLER argObjKey);
        }

        if (!dirInfo)
        {
            return false;
        }

        if (pFileInfo)
        {
            *pFileInfo = dirInfo->FileInfo;
        }

    }   // ���O�̃��b�N������ (safeShare �̐�������)

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    StatsIncr(listObjects);
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        return this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, pDirInfoList);

    }   // ���O�̃��b�N������ (safeShare �̐�������)
}

bool AwsS3::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());
    APP_ASSERT(pDirInfoList);

    DirInfoListType dirInfoList;

    if (!this->listObjects(CONT_CALLER argObjKey, &dirInfoList))
    {
        traceW(L"fault: listObjects");

        return false;
    }

    // CMD �Ɠ��������������邽�� ".", ".." �����݂��Ȃ��ꍇ�ɒǉ�����

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
    // HeadObject �Ŏ擾�����L���b�V���ƃ}�[�W
    //

    for (auto& dirInfo: dirInfoList)
    {
        if (wcscmp(dirInfo->FileNameBuf, L".") == 0 || wcscmp(dirInfo->FileNameBuf, L"..") == 0)
        {
            continue;
        }

        ObjectKey searchObjKey;

        if (FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
        {
            searchObjKey = argObjKey.append(dirInfo->FileNameBuf).toDir();
        }
        else
        {
            searchObjKey = argObjKey.append(dirInfo->FileNameBuf);
        }

        APP_ASSERT(searchObjKey.valid());
        APP_ASSERT(!searchObjKey.isBucket());

        UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, searchObjKey.str());   // ���O�ւ̎Q�Ƃ�o�^
        {
            const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

            if (mConfig.strictFileTimestamp)
            {
                // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject ���擾

                const auto mergeDirInfo{ this->unsafeHeadObjectWithCache(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;

                    //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                }
            }
            else
            {
                // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject �̃L���b�V��������

                const auto mergeDirInfo{ this->getCachedHeadObject(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;
                }
            }

            if (this->isNegativeHeadObject(CONT_CALLER searchObjKey))
            {
                // ���[�W�����Ⴂ�Ȃǂ� HeadObject �����s�������̂� HIDDEN ������ǉ�

                dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

        }   // ���O�̃��b�N������ (safeShare �̐�������)
    }

    *pDirInfoList = std::move(dirInfoList);

    return true;
}

// EOF