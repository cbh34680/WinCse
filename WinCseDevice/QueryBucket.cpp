#include "QueryBucket.hpp"

using namespace CSELIB;

namespace CSEDVC {

void QueryBucket::qbClearCache(CALLER_ARG0)
{
    mCacheListBuckets.clbClear(CONT_CALLER0);
}

void QueryBucket::qbReportCache(CALLER_ARG FILE* fp) const
{
    mCacheListBuckets.clbReport(CONT_CALLER fp);
}

bool QueryBucket::qbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName, std::wstring* pBucketRegion)
{
    NEW_LOG_BLOCK();

    traceW(L"argBucketName: %s", argBucketName.c_str());

    std::wstring bucketRegion;

    if (!mCacheListBuckets.clbGetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
    {
        // �L���b�V���ɑ��݂��Ȃ�

        if (!mApiClient->GetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
        {
            errorW(L"fault: GetBucketRegion");
            return false;
        }

        APP_ASSERT(!bucketRegion.empty());

        mCacheListBuckets.clbAddBucketRegion(CONT_CALLER argBucketName, bucketRegion);
    }

    APP_ASSERT(!bucketRegion.empty());

    *pBucketRegion = std::move(bucketRegion);

    return true;
}

bool QueryBucket::qbHeadBucket(CALLER_ARG const std::wstring& argBucketName, DirEntryType* pDirEntry)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    traceW(L"argBucketName: %s", argBucketName.c_str());

    std::wstring bucketRegion;

    if (this->qbGetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
    {
        if (!mApiClient->canAccessRegion(CONT_CALLER bucketRegion))
        {
            // �o�P�b�g�̃��[�W�������قȂ�Ƃ��͋���

            traceW(L"%s: no match bucket-region", bucketRegion.c_str());
            return false;
        }
    }
    else
    {
        errorW(L"fault: qbGetBucketRegion");
        return false;
    }

    // �L���b�V������T��
    // --> �L���b�V���ɑ��݂��Ȃ��󋵂͔������Ȃ��͂��Ȃ̂ŁAclbFind �̎��s�̂�

    return mCacheListBuckets.clbFind(CONT_CALLER argBucketName, pDirEntry);
}

bool QueryBucket::qbListBuckets(CALLER_ARG DirEntryListType* pDirEntryList)
{
    NEW_LOG_BLOCK();

    DirEntryListType dirEntryList;

    if (mCacheListBuckets.clbEmpty(CONT_CALLER0))
    {
        const auto now{ std::chrono::system_clock::now() };
        const auto lastSetTime{ mCacheListBuckets.clbGetLastSetTime(CONT_CALLER0) };

        const auto elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSetTime).count();

        if (elapsedMillis < TIMEMILLIS_1MINll)
        {
            // �o�P�b�g�ꗗ����ł����(�l�K�e�B�u)�L���b�V�� �̗L�������� (1 ��)

            traceW(L"empty buckets, short time cache, diff=%lld", TIMEMILLIS_1MINll - elapsedMillis);
            return true;
        }

        //traceW(L"cache empty");

        // �o�P�b�g�ꗗ�̎擾

        if (!mApiClient->ListBuckets(CONT_CALLER &dirEntryList))
        {
            errorW(L"fault: ListBuckets");
            return false;
        }

        //traceW(L"update cache");

        // �L���b�V���ɃR�s�[

        mCacheListBuckets.clbSet(CONT_CALLER dirEntryList);
    }
    else
    {
        // �L���b�V������R�s�[

        mCacheListBuckets.clbGet(CONT_CALLER0, &dirEntryList);

        //traceW(L"use cache: size=%zu", dirEntryList.size());
    }

    if (pDirEntryList)
    {
        for (auto it=dirEntryList.cbegin(); it!=dirEntryList.cend(); )
        {
            const auto& bucketName{ (*it)->mName };

            std::wstring bucketRegion;

            if (mRuntimeEnv->StrictBucketRegion)
            {
                if (!this->qbGetBucketRegion(CONT_CALLER bucketName, &bucketRegion))
                {
                    errorW(L"fault: qbGetBucketRegion bucketName=%s***", SafeSubStringW(bucketName, 0, 3).c_str());
                    return false;
                }
            }
            else
            {
                // ���[�W�����E�L���b�V������̂ݎ擾

                if (mCacheListBuckets.clbGetBucketRegion(CONT_CALLER bucketName, &bucketRegion))
                {
                    APP_ASSERT(!bucketRegion.empty());
                }
            }

            if (!bucketRegion.empty())
            {
                // �قȂ郊�[�W�����ł��邩���ׂ�

                if (!mApiClient->canAccessRegion(CONT_CALLER bucketRegion))
                {
                    // ���[�W�������قȂ�ꍇ�� HIDDEN ������t�^
                    //
                    // --> headBucket() �Ń��[�W�������擾���Ă���̂ŁA�o�P�b�g�E�L���b�V���쐬���ł͂ł��Ȃ�

                    (*it)->mFileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }
            }

            ++it;
        }

        *pDirEntryList = std::move(dirEntryList);
    }

    return true;
}

bool QueryBucket::qbReload(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    NEW_LOG_BLOCK();

    const auto lastSetTime = mCacheListBuckets.clbGetLastSetTime(CONT_CALLER0);

    if (threshold < lastSetTime)
    {
        // �L���b�V���̗L��������

        traceW(L"bucket cache is valid");
    }
    else
    {
        // �o�P�b�g�E�L���b�V�����쐬���Ă����莞�Ԃ��o��

        traceW(L"RELOAD");

        // �o�P�b�g�̃L���b�V�����폜���āA�ēx�ꗗ���擾����

        mCacheListBuckets.clbClear(CONT_CALLER0);

        // �o�P�b�g�ꗗ�̎擾 --> �L���b�V���̐���

        if (!this->qbListBuckets(CONT_CALLER nullptr))
        {
            errorW(L"fault: listBuckets");
            return false;
        }
    }

    return true;
}

}   // namespace CSEDVC

// EOF