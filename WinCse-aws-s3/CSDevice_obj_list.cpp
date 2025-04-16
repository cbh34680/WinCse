#include "CSDevice.hpp"

using namespace WCSE;

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

//
// �������牺�̃��\�b�h�� UnprotectedShare �ɂ��r�����䂪�K�v
//
//static std::mutex gGuard;
//#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }

// �r������̕K�v�Ȕ͈͂����肷�邽�߁A�O���[�o���ϐ��ɂ��Ă���

struct ObjectListShare : public SharedBase { };
static ShareStore<ObjectListShare> gObjectListShare;

bool CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo)
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

        if (argObjKey.meansDir())
        {
            return mQueryObject->unsafeHeadObject_CheckDir(CONT_CALLER argObjKey, pDirInfo);
        }
        else
        {
            APP_ASSERT(argObjKey.meansFile());

            return mQueryObject->unsafeHeadObject(CONT_CALLER argObjKey, pDirInfo);
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
            dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir1(L".."));
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
        dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir1(L"."));
    }
    else
    {
        const auto save{ *itCurr };
        dirInfoList.erase(itCurr);
        dirInfoList.insert(dirInfoList.cbegin(), save);
    }

    //
    // dirInfoList �̓��e�� HeadObject �Ŏ擾�����L���b�V���ƃ}�[�W
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

            DirInfoType mergeDirInfo;

            if (mRuntimeEnv->StrictFileTimestamp)
            {
                // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject ���擾

                mQueryObject->unsafeHeadObject(CONT_CALLER searchObjKey, &mergeDirInfo);
            }
            else
            {
                // �f�B���N�g���Ƀt�@�C������t�^���� HeadObject �̃L���b�V��������

                mQueryObject->headObjectFromCache(CONT_CALLER searchObjKey, &mergeDirInfo);
            }

            if (mergeDirInfo)
            {
                dirInfo->FileInfo = mergeDirInfo->FileInfo;
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

// �C���M�����[�ȑΉ�
// 
// ���X�� CSDevice_obj_rw.cpp �ɋL�q����Ă������AXCOPY /V �����s�����Ƃ���
// �N���[�Y�������� listObjectsV2 ���Ă΂�A�L���b�V���X�V�O�̃t�@�C���T�C�Y�����p����邱�Ƃ�
// ���؂Ɏ��s���Ă��܂��B
// ����ɑΉ����邽�߁APutObject �ƃL���b�V���̍폜�� listObjectsV2 �̃��b�N�Ɠ����͈͂ōs���B

bool CSDevice::putObjectWithListLock(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    // �ʏ�͑���Ώۂƃ��b�N�̃L�[����v���邪�A�����ł͐e�̃f�B���N�g�������b�N���Ȃ���
    // �f�B���N�g�����̃t�@�C���𑀍삵�Ă���B

    const auto parentDir{ argObjKey.toParentDir() };
    APP_ASSERT(parentDir);

    UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, parentDir->str() };   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        if (!mExecuteApi->PutObject(CONT_CALLER argObjKey, argFileInfo, argSourcePath))
        {
            traceW(L"fault: PutObject");
            return false;
        }

        // �L���b�V���E����������폜
        //
        // ��L�ō쐬�����f�B���N�g�����L���b�V���ɔ��f����Ă��Ȃ���Ԃ�
        // ���p����Ă��܂����Ƃ�������邽�߂Ɏ��O�ɍ폜���Ă����A���߂ăL���b�V�����쐬������

        const auto num = mQueryObject->deleteCache(CONT_CALLER argObjKey);
        traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());
    }

    // headObject() �͕K�{�ł͂Ȃ����A�쐬����ɑ������Q�Ƃ���邱�ƂɑΉ�

    if (!this->headObject(CONT_CALLER argObjKey, nullptr))
    {
        traceW(L"fault: headObject");
        return false;
    }

    return true;
}

// EOF