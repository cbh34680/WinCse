#include "QueryObject.hpp"

using namespace CSELIB;
using namespace CSEDAS3;


bool QueryObject::qoHeadObjectFromCache(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) const noexcept
{
    return mCacheHeadObject.coGet(CONT_CALLER argObjKey, pDirInfo);
}

bool QueryObject::qoIsInNegativeCache(CALLER_ARG const ObjectKey& argObjKey) const noexcept
{
    return mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey); 
}

void QueryObject::qoReportCache(CALLER_ARG FILE* fp) const noexcept
{
    mCacheHeadObject.coReport(CONT_CALLER fp);
    mCacheListObjects.coReport(CONT_CALLER fp);
}

int QueryObject::qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept
{
    const auto delHead = mCacheHeadObject.coDeleteByTime(CONT_CALLER threshold);
    const auto delList = mCacheListObjects.coDeleteByTime(CONT_CALLER threshold);

    return delHead + delList;
}

int QueryObject::qoClearCache(CALLER_ARG0) noexcept
{
    const auto now{ std::chrono::system_clock::now() };

    return this->qoDeleteOldCache(CONT_CALLER now);
}

int QueryObject::qoDeleteCache(CALLER_ARG const ObjectKey& argObjKey) noexcept
{
    const auto delHead = mCacheHeadObject.coDeleteByKey(CONT_CALLER argObjKey);
    const auto delList = mCacheListObjects.coDeleteByKey(CONT_CALLER argObjKey);

    return delHead + delList;
}

bool QueryObject::qoHeadObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) noexcept
{
    APP_ASSERT(!argObjKey.isBucket());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoPtr dirInfo;

    if (mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirInfo))
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
    }
    else
    {
        NEW_LOG_BLOCK();

        // HeadObject API �̎��s

        if (!mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirInfo))
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            traceW(L"fault: headObject");

            mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        traceW(L"success: HeadObject");

        // �L���b�V���ɃR�s�[

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    if (pDirInfo)
    {
        *pDirInfo = std::move(dirInfo);
    }

    return true;
}

bool QueryObject::qoHeadObjectOrListObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) noexcept
{
    APP_ASSERT(argObjKey.isObject());
    APP_ASSERT(argObjKey.meansDir());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoPtr dirInfo;

    if (mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirInfo))
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
    }
    else
    {
        NEW_LOG_BLOCK();

        if (mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirInfo))
        {
            // ��̃f�B���N�g���E�I�u�W�F�N�g(ex. "dir/") �����݂����

            traceW(L"success: HeadObject");
        }
        else
        {
            // ���ʂ̑w�ɃI�u�W�F�N�g�����݂��邪�A���w�ɋ�̃f�B���N�g���E�I�u�W�F�N�g
            // �͑��݂��Ȃ���
            //
            // ��)
            //  "s3://bucket/dir/" �����݂��Ȃ���ԂŁA�ȉ������s
            //
            //      $ aws s3 cp file.txt s3://bucket/dir/subdir/

            traceW(L"fault: HeadObject");

            // �e�f�B���N�g���� CommonPrefix ����f�B���N�g�������擾

            std::wstring parentDir;
            std::wstring searchName;

            const auto b = SplitObjectKey(argObjKey.str(), &parentDir, &searchName);
            APP_ASSERT(b);

            APP_ASSERT(!parentDir.empty());
            APP_ASSERT(!searchName.empty());
            APP_ASSERT(searchName.back() == L'/');

            const auto optParentDir{ ObjectKey::fromPath(parentDir) };

            if (!optParentDir)      // �R���p�C���̌x���}�~
            {
                APP_ASSERT(0);

                return false;
            }

            DirInfoPtrList dirInfoList;

            // �e�f�B���N�g���̃L���b�V�����璲�ׂ�
            // --> �ʏ�͂����ɂ���͂�

            if (!mCacheListObjects.coGet(CONT_CALLER *optParentDir, &dirInfoList))
            {
                // ���݂��Ȃ��ꍇ�͐e�̃f�B���N�g���ɑ΂��� ListObjectsV2() API �����s����

                if (!mExecuteApi->ListObjectsV2(CONT_CALLER *optParentDir, true, 0, &dirInfoList))
                {
                    // �G���[�̎��̓l�K�e�B�u�E�L���b�V���ɓo�^

                    traceW(L"fault: ListObjectsV2");

                    mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                    return false;
                }
            }

            // �e�f�B���N�g���̃��X�g���疼�O�̈�v������̂�T��

            for (const auto& it: dirInfoList)
            {
                if (it->FileName == searchName)
                {
                    // TODO: ������ FileAttributes ���`�F�b�N

                    dirInfo = makeDirInfoOfDir(it->FileName, it->FileInfo.LastWriteTime, it->FileInfo.FileAttributes | mRuntimeEnv->DefaultFileAttributes);
                    break;
                }
            }

            if (!dirInfo)
            {
                // ������Ȃ�������l�K�e�B�u�E�L���b�V���ɓo�^

                traceW(L"not found in Parent CommonPrefix");

                mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                return false;
            }
        }

        APP_ASSERT(dirInfo);

        // �L���b�V���ɃR�s�[

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    if (pDirInfo)
    {
        *pDirInfo = std::move(dirInfo);
    }

    return true;
}

bool QueryObject::qoListObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtrList* pDirInfoList) noexcept
{
    APP_ASSERT(argObjKey.meansDir());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mCacheListObjects.coIsNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoPtrList dirInfoList;

    if (mCacheListObjects.coGet(CONT_CALLER argObjKey, &dirInfoList))
    {
        // �|�W�e�B�u�E�L���b�V���Ɍ�������
    }
    else
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�����Ȃ�

        if (!mExecuteApi->ListObjectsV2(CONT_CALLER argObjKey, true, 0, &dirInfoList))
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            NEW_LOG_BLOCK();
            traceW(L"fault: ListObjectsV2");

            mCacheListObjects.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        NEW_LOG_BLOCK();
        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheListObjects.coSet(CONT_CALLER argObjKey, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// EOF