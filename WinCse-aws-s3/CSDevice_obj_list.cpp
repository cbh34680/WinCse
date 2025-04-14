#include "CSDevice.hpp"

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

DirInfoType CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey)
{
    //THREAD_SAFE();
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // �f�B���N�g���̑��݊m�F

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂�
    // �{���͊O������ listObjects() �����s���āA���W�b�N�Ŕ��f���邪
    // �Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, argObjKey.str() };   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        DirInfoType dirInfo;

        if (argObjKey.meansDir())
        {
            return mQueryObject->unsafeHeadObject_CheckDir(CONT_CALLER argObjKey);
        }
        else
        {
            APP_ASSERT(argObjKey.meansFile());

            return mQueryObject->unsafeHeadObject(CONT_CALLER argObjKey);
        }

    }   // ���O�̃��b�N������ (safeShare �̐�������)
}

bool CSDevice::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, argObjKey.str() };   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        return mQueryObject->unsafeListObjects(CONT_CALLER argObjKey, pDirInfoList);

    }   // ���O�̃��b�N������ (safeShare �̐�������)
}

bool CSDevice::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
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

    if (argObjKey.isObject())
    {
        const auto itParent = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
        {
            return wcscmp(dirInfo->FileNameBuf, L"..") == 0;
        });

        if (itParent == dirInfoList.cend())
        {
            dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir(L".."));
        }
        else
        {
            const auto save{ *itParent };
            dirInfoList.erase(itParent);
            dirInfoList.insert(dirInfoList.cbegin(), save);
        }
    }

    const auto itCurr = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
    {
        return wcscmp(dirInfo->FileNameBuf, L".") == 0;
    });

    if (itCurr == dirInfoList.cend())
    {
        dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir(L"."));
    }
    else
    {
        const auto save{ *itCurr };
        dirInfoList.erase(itCurr);
        dirInfoList.insert(dirInfoList.cbegin(), save);
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

        if (FA_IS_DIRECTORY(dirInfo->FileInfo.FileAttributes))
        {
            searchObjKey = argObjKey.append(dirInfo->FileNameBuf).toDir();
        }
        else
        {
            searchObjKey = argObjKey.append(dirInfo->FileNameBuf);
        }

        APP_ASSERT(searchObjKey.isObject());

        UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, searchObjKey.str() };   // ���O�ւ̎Q�Ƃ�o�^
        {
            const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

            if (mRuntimeEnv->StrictFileTimestamp)
            {
                // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject ���擾

                const auto mergeDirInfo{ mQueryObject->unsafeHeadObject(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;

                    //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                }
            }
            else
            {
                // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject �̃L���b�V��������

                const auto mergeDirInfo{ mQueryObject->headObjectCacheOnly(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;
                }
            }

            if (mQueryObject->isNegative(CONT_CALLER searchObjKey))
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