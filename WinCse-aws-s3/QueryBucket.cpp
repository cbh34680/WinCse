#include "QueryBucket.hpp"

using namespace WCSE;


#define REMOVE_BUCKET_OTHER_REGION      (0)

#if REMOVE_BUCKET_OTHER_REGION
struct NotifRemoveBucketTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::Yes; }
    Priority getPriority() const noexcept override { return Priority::Low; }

    FSP_FILE_SYSTEM* mFileSystem;
    const std::wstring mFileName;

    NotifRemoveBucketTask(FSP_FILE_SYSTEM* argFileSystem, const std::wstring& argFileName)
        : mFileSystem(argFileSystem), mFileName(argFileName) { }

    std::wstring synonymString() const noexcept override
    {
        return std::wstring(L"NotifyRemoveBucketTask; ") + mFileName;
    }

    void run(CALLER_ARG0) override
    {
        NEW_LOG_BLOCK();

        traceW(L"exec FspFileSystemNotify**");

        NTSTATUS ntstatus = FspFileSystemNotifyBegin(mFileSystem, 1000UL);
        if (NT_SUCCESS(ntstatus))
        {
            union
            {
                FSP_FSCTL_NOTIFY_INFO V;
                UINT8 B[1024];
            } Buffer{};
            ULONG Length = 0;
            union
            {
                FSP_FSCTL_NOTIFY_INFO V;
                UINT8 B[sizeof(FSP_FSCTL_NOTIFY_INFO) + MAX_PATH * sizeof(WCHAR)];
            } NotifyInfo{};

            NotifyInfo.V.Size = (UINT16)(sizeof(FSP_FSCTL_NOTIFY_INFO) + mFileName.length() * sizeof(WCHAR));
            NotifyInfo.V.Filter = FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_ACTION_MODIFIED;
            NotifyInfo.V.Action = FILE_ACTION_REMOVED;
            memcpy(NotifyInfo.V.FileNameBuf, mFileName.c_str(), NotifyInfo.V.Size - sizeof(FSP_FSCTL_NOTIFY_INFO));

            FspFileSystemAddNotifyInfo(&NotifyInfo.V, &Buffer, sizeof Buffer, &Length);

            ntstatus = FspFileSystemNotify(mFileSystem, &Buffer.V, Length);
            //APP_ASSERT(STATUS_SUCCESS == ntstatus);

            ntstatus = FspFileSystemNotifyEnd(mFileSystem);
            //APP_ASSERT(STATUS_SUCCESS == ntstatus);
        }
    }
};
#endif


void QueryBucket::clearCache(CALLER_ARG0) noexcept
{
    mCacheListBuckets.clear(CONT_CALLER0);
}

void QueryBucket::reportCache(CALLER_ARG FILE* fp) const noexcept
{
    mCacheListBuckets.report(CONT_CALLER fp);
}

std::wstring QueryBucket::unsafeGetBucketRegion(CALLER_ARG const std::wstring& argBucketName) noexcept
{
    //NEW_LOG_BLOCK();

    //traceW(L"bucketName: %s", bucketName.c_str());

    std::wstring bucketRegion;

    if (mCacheListBuckets.getBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
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

        mCacheListBuckets.addBucketRegion(CONT_CALLER argBucketName, bucketRegion);
    }

    return bucketRegion;
}

bool QueryBucket::unsafeHeadBucket(CALLER_ARG const std::wstring& argBucketName, DirInfoType* pDirInfo) noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    //traceW(L"bucket: %s", bucketName.c_str());

    const auto bucketRegion{ this->unsafeGetBucketRegion(CONT_CALLER argBucketName) };
    if (bucketRegion != mRuntimeEnv->ClientRegion)
    {
        // バケットのリージョンが異なるときは拒否

        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

#if REMOVE_BUCKET_OTHER_REGION
        // 非表示になるバケットについて WinFsp に通知
        getWorker(L"delayed")->addTask(START_CALLER new NotifRemoveBucketTask{ mFileSystem, std::wstring(L"\\") + argBucketName });
#endif

        return false;
    }

    // キャッシュから探す

    DirInfoType dirInfo;

    if (!mCacheListBuckets.find(CONT_CALLER argBucketName, &dirInfo))
    {
        return false;
    }

    if (pDirInfo)
    {
        *pDirInfo = std::move(dirInfo);
    }

    return true;
}

bool QueryBucket::unsafeListBuckets(CALLER_ARG
    WCSE::DirInfoListType* pDirInfoList, const std::vector<std::wstring>& options) noexcept
{
    NEW_LOG_BLOCK();

    DirInfoListType dirInfoList;

    if (mCacheListBuckets.empty(CONT_CALLER0))
    {
        const auto now{ std::chrono::system_clock::now() };
        const auto lastSetTime{ mCacheListBuckets.getLastSetTime(CONT_CALLER0) };

        const auto elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSetTime).count();

        if (elapsedMillis < TIMEMILLIS_1MIN)
        {
            // バケット一覧が空である状況のキャッシュ有効期限内 (1 分)

            traceW(L"empty buckets, short time cache, diff=%lld", TIMEMILLIS_1MIN - elapsedMillis);
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

        mCacheListBuckets.set(CONT_CALLER dirInfoList);
    }
    else
    {
        // キャッシュからコピー

        mCacheListBuckets.get(CONT_CALLER0, &dirInfoList);

        //traceW(L"use cache: size=%zu", dirInfoList.size());
    }

    if (pDirInfoList)
    {
        for (auto it=dirInfoList.cbegin(); it!=dirInfoList.cend(); )
        {
            const std::wstring bucketName{ (*it)->FileNameBuf };

            std::wstring bucketRegion;

            if (mRuntimeEnv->StrictBucketRegion)
            {
                // キャッシュに存在しなければ API を実行

                bucketRegion = this->unsafeGetBucketRegion(CONT_CALLER bucketName);

                APP_ASSERT(!bucketRegion.empty());
            }
            else
            {
                // リージョン・キャッシュからのみ取得

                if (mCacheListBuckets.getBucketRegion(CONT_CALLER bucketName, &bucketRegion))
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
                const auto itOpts{ std::find(options.cbegin(), options.cend(), bucketName) };
                if (itOpts == options.cend())
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

bool QueryBucket::unsafeReload(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept
{
    const auto lastSetTime = mCacheListBuckets.getLastSetTime(CONT_CALLER0);

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

        mCacheListBuckets.clear(CONT_CALLER0);

        // バケット一覧の取得 --> キャッシュの生成

        if (!this->unsafeListBuckets(CONT_CALLER nullptr, {}))
        {
            traceW(L"fault: listBuckets");
            return false;
        }
    }

    return true;
}

// EOF