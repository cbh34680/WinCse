#include "CSDevice.hpp"
#include "Protect.hpp"

using namespace WCSE;

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

bool CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo)
{
    APP_ASSERT(argObjKey.isObject());

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂�
    // �{���͊O������ listObjects() �����s���āA���W�b�N�Ŕ��f���邪
    // �Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    if (argObjKey.meansDir())
    {
        return mQueryObject->unsafeHeadObject_CheckDir(CONT_CALLER argObjKey, pDirInfo);
    }
    else
    {
        APP_ASSERT(argObjKey.meansFile());

        return mQueryObject->unsafeHeadObject(CONT_CALLER argObjKey, pDirInfo);
    }
}

bool CSDevice::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    APP_ASSERT(argObjKey.meansDir());
    APP_ASSERT(pDirInfoList);

    DirInfoListType dirInfoList;

    if (!this->listObjects(CONT_CALLER argObjKey, &dirInfoList))
    {
        NEW_LOG_BLOCK();
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
    }

    *pDirInfoList = std::move(dirInfoList);

    return true;
}

#if XCOPY_V

struct CloseGuard : public SharedBase { };
static ShareStore<CloseGuard> gCloseGuard;

bool CSDevice::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    APP_ASSERT(argObjKey.meansDir());

    UnprotectedShare<CloseGuard> unsafeShare{ &gCloseGuard, argObjKey.c_str() };
    {
        const auto safeShare{ unsafeShare.lock() };

        return mQueryObject->unsafeListObjects(CONT_CALLER argObjKey, pDirInfoList);
    }
}

bool CSDevice::putObjectViaListLock(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    const auto parentDir{ argObjKey.toParentDir() };
    APP_ASSERT(parentDir);

    UnprotectedShare<CloseGuard> unsafeShare{ &gCloseGuard, parentDir->c_str() };
    {
        const auto safeShare{ unsafeShare.lock() };

        if (!this->putObject(CONT_CALLER argObjKey, argFileInfo, argSourcePath))
        {
            traceW(L"fault: putObject");
            return false;
        }
    }

    return true;
}

#else
bool CSDevice::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    APP_ASSERT(argObjKey.meansDir());

    return mQueryObject->unsafeListObjects(CONT_CALLER argObjKey, pDirInfoList);
}

#endif

// EOF