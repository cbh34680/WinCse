#include "QueryBucket.hpp"

using namespace CSELIB;
using namespace CSEDAS3;


void QueryBucket::qbClearCache(CALLER_ARG0) noexcept
{
    mCacheListBuckets.clbClear(CONT_CALLER0);
}

void QueryBucket::qbReportCache(CALLER_ARG FILE* fp) const noexcept
{
    mCacheListBuckets.clbReport(CONT_CALLER fp);
}

std::wstring QueryBucket::qbGetBucketRegion(CALLER_ARG const std::wstring& argBucketName) noexcept
{
    //NEW_LOG_BLOCK();

    //traceW(L"bucketName: %s", bucketName.c_str());

    std::wstring bucketRegion;

    if (mCacheListBuckets.clbGetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
    {
        APP_ASSERT(!bucketRegion.empty());

        //traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }
    else
    {
        // �L���b�V���ɑ��݂��Ȃ�

        if (!mExecuteApi->GetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
        {
            // �擾�ł��Ȃ��Ƃ��̓f�t�H���g�l�ɂ���

            bucketRegion = MB2WC(AWS_DEFAULT_REGION);

            //traceW(L"error, fall back region is %s", bucketRegion.c_str());
        }

        APP_ASSERT(!bucketRegion.empty());

        mCacheListBuckets.clbAddBucketRegion(CONT_CALLER argBucketName, bucketRegion);
    }

    return bucketRegion;
}

bool QueryBucket::qbHeadBucket(CALLER_ARG const std::wstring& argBucketName, DirInfoPtr* pDirInfo) noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    //traceW(L"bucket: %s", bucketName.c_str());

    const auto bucketRegion{ this->qbGetBucketRegion(CONT_CALLER argBucketName) };
    if (bucketRegion != mRuntimeEnv->ClientRegion)
    {
        // �o�P�b�g�̃��[�W�������قȂ�Ƃ��͋���

        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

        return false;
    }

    // �L���b�V������T��

    DirInfoPtr dirInfo;

    if (!mCacheListBuckets.clbFind(CONT_CALLER argBucketName, &dirInfo))
    {
        return false;
    }

    if (pDirInfo)
    {
        *pDirInfo = std::move(dirInfo);
    }

    return true;
}

bool QueryBucket::qbListBuckets(CALLER_ARG
    CSELIB::DirInfoPtrList* pDirInfoList, const std::set<std::wstring>& options) noexcept
{
    NEW_LOG_BLOCK();

    DirInfoPtrList dirInfoList;

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

        if (!mExecuteApi->ListBuckets(CONT_CALLER &dirInfoList))
        {
            traceW(L"fault: ListBuckets");
            return false;
        }

        //traceW(L"update cache");

        // �L���b�V���ɃR�s�[

        mCacheListBuckets.clbSet(CONT_CALLER dirInfoList);
    }
    else
    {
        // �L���b�V������R�s�[

        mCacheListBuckets.clbGet(CONT_CALLER0, &dirInfoList);

        //traceW(L"use cache: size=%zu", dirInfoList.size());
    }

    if (pDirInfoList)
    {
        for (auto it=dirInfoList.cbegin(); it!=dirInfoList.cend(); )
        {
            const auto bucketName{ (*it)->FileName };

            std::wstring bucketRegion;

            if (mRuntimeEnv->StrictBucketRegion)
            {
                // �L���b�V���ɑ��݂��Ȃ���� API �����s

                bucketRegion = this->qbGetBucketRegion(CONT_CALLER bucketName);

                APP_ASSERT(!bucketRegion.empty());
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

                if (bucketRegion != mRuntimeEnv->ClientRegion)
                {
                    // ���[�W�������قȂ�ꍇ�� HIDDEN ������t�^
                    //
                    // --> headBucket() �Ń��[�W�������擾���Ă���̂ŁA�o�P�b�g�E�L���b�V���쐬���ł͂ł��Ȃ�

                    (*it)->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }
            }

            if (!options.empty())
            {
                // ������ options ���w�肳��Ă���ꍇ

                if (options.find(bucketName) == options.cend())
                {
                    // �������o�����Ɉ�v���Ȃ��ꍇ�͎�菜��

                    it = dirInfoList.erase(it);

                    continue;
                }
            }

            ++it;
        }

        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

bool QueryBucket::qbReload(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept
{
    const auto lastSetTime = mCacheListBuckets.clbGetLastSetTime(CONT_CALLER0);

    if (threshold < lastSetTime)
    {
        // �L���b�V���̗L��������

        //traceW(L"bucket cache is valid");
    }
    else
    {
        NEW_LOG_BLOCK();

        // �o�P�b�g�E�L���b�V�����쐬���Ă����莞�Ԃ��o��

        traceW(L"RELOAD");

        // �o�P�b�g�̃L���b�V�����폜���āA�ēx�ꗗ���擾����

        mCacheListBuckets.clbClear(CONT_CALLER0);

        // �o�P�b�g�ꗗ�̎擾 --> �L���b�V���̐���

        if (!this->qbListBuckets(CONT_CALLER nullptr, {}))
        {
            traceW(L"fault: listBuckets");
            return false;
        }
    }

    return true;
}

// EOF