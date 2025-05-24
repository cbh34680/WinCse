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
        // キャッシュに存在しない

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

bool QueryBucket::qbHeadBucket(CALLER_ARG const std::wstring& argBucketName, CSELIB::DirEntryType* pDirEntry)
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
            // バケットのリージョンが異なるときは拒否

            traceW(L"%s: no match bucket-region", bucketRegion.c_str());
            return false;
        }
    }
    else
    {
        errorW(L"fault: qbGetBucketRegion");
        return false;
    }

    // キャッシュから探す
    // --> キャッシュに存在しない状況は発生しないはずなので、clbFind の実行のみ

    return mCacheListBuckets.clbFind(CONT_CALLER argBucketName, pDirEntry);
}

bool QueryBucket::qbListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList)
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
            // バケット一覧が空である状況(ネガティブ)キャッシュ の有効期限内 (1 分)

            traceW(L"empty buckets, short time cache, diff=%lld", TIMEMILLIS_1MINll - elapsedMillis);
            return true;
        }

        //traceW(L"cache empty");

        // バケット一覧の取得

        if (!mApiClient->ListBuckets(CONT_CALLER &dirEntryList))
        {
            errorW(L"fault: ListBuckets");
            return false;
        }

        //traceW(L"update cache");

        // キャッシュにコピー

        mCacheListBuckets.clbSet(CONT_CALLER dirEntryList);
    }
    else
    {
        // キャッシュからコピー

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
                // リージョン・キャッシュからのみ取得

                if (mCacheListBuckets.clbGetBucketRegion(CONT_CALLER bucketName, &bucketRegion))
                {
                    APP_ASSERT(!bucketRegion.empty());
                }
            }

            if (!bucketRegion.empty())
            {
                // 異なるリージョンであるか調べる

                if (!mApiClient->canAccessRegion(CONT_CALLER bucketRegion))
                {
                    // リージョンが異なる場合は HIDDEN 属性を付与
                    //
                    // --> headBucket() でリージョンを取得しているので、バケット・キャッシュ作成時ではできない

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
        // キャッシュの有効期限内

        traceW(L"bucket cache is valid");
    }
    else
    {
        // バケット・キャッシュを作成してから一定時間が経過

        traceW(L"RELOAD");

        // バケットのキャッシュを削除して、再度一覧を取得する

        mCacheListBuckets.clbClear(CONT_CALLER0);

        // バケット一覧の取得 --> キャッシュの生成

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