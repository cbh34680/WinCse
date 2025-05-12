#include "QueryObject.hpp"

using namespace CSELIB;
using namespace CSESS3;


bool QueryObject::qoHeadObjectFromCache(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry) const
{
    return mCacheHeadObject.coGet(CONT_CALLER argObjKey, pDirEntry);
}

bool QueryObject::qoIsInNegativeCache(CALLER_ARG const ObjectKey& argObjKey) const
{
    return mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey); 
}

void QueryObject::qoReportCache(CALLER_ARG FILE* fp) const
{
    mCacheHeadObject.coReport(CONT_CALLER fp);
    mCacheListObjects.coReport(CONT_CALLER fp);
}

int QueryObject::qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    const auto delHead = mCacheHeadObject.coDeleteByTime(CONT_CALLER threshold);
    const auto delList = mCacheListObjects.coDeleteByTime(CONT_CALLER threshold);

    return delHead + delList;
}

int QueryObject::qoClearCache(CALLER_ARG0)
{
    const auto now{ std::chrono::system_clock::now() };

    return this->qoDeleteOldCache(CONT_CALLER now);
}

int QueryObject::qoDeleteCache(CALLER_ARG const ObjectKey& argObjKey)
{
    const auto delHead = mCacheHeadObject.coDeleteByKey(CONT_CALLER argObjKey);
    const auto delList = mCacheListObjects.coDeleteByKey(CONT_CALLER argObjKey);

    return delHead + delList;
}

bool QueryObject::qoHeadObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argObjKey.isBucket());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirEntryType dirEntry;

    if (mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirEntry))
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
    }
    else
    {
        // HeadObject API �̎��s

        if (!mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirEntry))
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            // ���݃`�F�b�N�����˂Ă���̂ŁA������ traceW() �̂܂�
            traceW(L"not found: headObject argObjKey=%s", argObjKey.c_str());

            mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        // �L���b�V���ɃR�s�[

        traceW(L"coSet argObjKey=%s dirEntry=%s", argObjKey.c_str(), dirEntry->str().c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirEntry);
    }

    APP_ASSERT(dirEntry);

    if (pDirEntry)
    {
        *pDirEntry = std::move(dirEntry);
    }

    return true;
}

bool QueryObject::qoHeadObjectOrListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argObjKey.isObject());
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirEntryType dirEntry;

    if (!mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirEntry))
    {
        if (!mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirEntry))
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

            if (!SplitObjectKey(argObjKey.str(), &parentDir, &searchName))
            {
                errorW(L"fault: SplitObjectKey argObjKey=%s", argObjKey.c_str());
                return false;
            }

            APP_ASSERT(!parentDir.empty());
            APP_ASSERT(!searchName.empty());
            APP_ASSERT(searchName.back() == L'/');

            const auto optParentDir{ ObjectKey::fromObjectPath(parentDir) };
            if (!optParentDir)
            {
                traceW(L"fault: fromObjectPath parentDir=%s", parentDir.c_str());
                return false;
            }

            DirEntryListType dirEntryList;

            // �e�f�B���N�g���̃L���b�V�����璲�ׂ�
            // --> �ʏ�͂����ɂ���͂�

            if (!mCacheListObjects.coGet(CONT_CALLER *optParentDir, &dirEntryList))
            {
                // ���݂��Ȃ��ꍇ�͐e�̃f�B���N�g���ɑ΂��� ListObjectsV2() API �����s����

                if (!mExecuteApi->ListObjects(CONT_CALLER *optParentDir, &dirEntryList))
                {
                    // �G���[�̎��̓l�K�e�B�u�E�L���b�V���ɓo�^

                    errorW(L"fault: ListObjects optParentDir=%s", optParentDir->c_str());

                    mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                    return false;
                }
            }

            // �e�f�B���N�g���̃��X�g���疼�O�̈�v������̂�T��

            const auto it = std::find_if(dirEntryList.cbegin(), dirEntryList.cend(), [&searchName](const auto& item)
            {
                return item->mName == searchName;
            });

            if (it == dirEntryList.cend())
            {
                // ������Ȃ�������l�K�e�B�u�E�L���b�V���ɓo�^

                traceW(L"fault: not found in Parent CommonPrefix argObjKey=%s", argObjKey.c_str());

                mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                return false;
            }

            dirEntry = *it;
        }

        APP_ASSERT(dirEntry);

        // �L���b�V���ɃR�s�[

        traceW(L"coSet argObjKey=%s dirEntry=%s", argObjKey.c_str(), dirEntry->str().c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirEntry);
    }

    APP_ASSERT(dirEntry);

    if (pDirEntry)
    {
        *pDirEntry = std::move(dirEntry);
    }

    return true;
}

bool QueryObject::qoListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mCacheListObjects.coIsNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirEntryListType dirEntryList;

    if (mCacheListObjects.coGet(CONT_CALLER argObjKey, &dirEntryList))
    {
        // �|�W�e�B�u�E�L���b�V���Ɍ�������
    }
    else
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�����Ȃ�

        if (!mExecuteApi->ListObjects(CONT_CALLER argObjKey, &dirEntryList))
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            errorW(L"fault: ListObjects argObjKey=%s", argObjKey.c_str());

            mCacheListObjects.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        traceW(L"coSet argObjKey=%s dirEntryList.size=%zu", argObjKey.c_str(), dirEntryList.size());

        mCacheListObjects.coSet(CONT_CALLER argObjKey, dirEntryList);
    }

    if (pDirEntryList)
    {
        *pDirEntryList = std::move(dirEntryList);
    }

    return true;
}

// EOF