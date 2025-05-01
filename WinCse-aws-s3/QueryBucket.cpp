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
        // キャッシュに存在しない

        if (!mExecuteApi->GetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
        {
            // 取得できないときはデフォルト値にする

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
        // バケットのリージョンが異なるときは拒否

        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

        return false;
    }

    // キャッシュから探す

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
            // バケット一覧が空である状況(ネガティブ)キャッシュ の有効期限内 (1 分)

            traceW(L"empty buckets, short time cache, diff=%lld", TIMEMILLIS_1MINll - elapsedMillis);
            return true;
        }

        //traceW(L"cache empty");

        // バケット一覧の取得

        if (!mExecuteApi->ListBuckets(CONT_CALLER &dirInfoList))
        {
            traceW(L"fault: ListBuckets");
            return false;
        }

        //traceW(L"update cache");

        // キャッシュにコピー

        mCacheListBuckets.clbSet(CONT_CALLER dirInfoList);
    }
    else
    {
        // キャッシュからコピー

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
                // キャッシュに存在しなければ API を実行

                bucketRegion = this->qbGetBucketRegion(CONT_CALLER bucketName);

                APP_ASSERT(!bucketRegion.empty());
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

                if (bucketRegion != mRuntimeEnv->ClientRegion)
                {
                    // リージョンが異なる場合は HIDDEN 属性を付与
                    //
                    // --> headBucket() でリージョンを取得しているので、バケット・キャッシュ作成時ではできない

                    (*it)->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }
            }

            if (!options.empty())
            {
                // 引数で options が指定されている場合

                if (options.find(bucketName) == options.cend())
                {
                    // 検索抽出条件に一致しない場合は取り除く

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
        // キャッシュの有効期限内

        //traceW(L"bucket cache is valid");
    }
    else
    {
        NEW_LOG_BLOCK();

        // バケット・キャッシュを作成してから一定時間が経過

        traceW(L"RELOAD");

        // バケットのキャッシュを削除して、再度一覧を取得する

        mCacheListBuckets.clbClear(CONT_CALLER0);

        // バケット一覧の取得 --> キャッシュの生成

        if (!this->qbListBuckets(CONT_CALLER nullptr, {}))
        {
            traceW(L"fault: listBuckets");
            return false;
        }
    }

    return true;
}

// EOF