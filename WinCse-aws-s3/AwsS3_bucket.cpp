#include "AwsS3.hpp"
#include "BucketCache.hpp"


using namespace WCSE;

/*
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

        traceW(L"*** exec FspFileSystemNotify mFileName=%s ***", mFileName.c_str());

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
*/

static BucketCache gBucketCache;

std::wstring AwsS3::getBucketLocation(CALLER_ARG const std::wstring& bucketName)
{
    //NEW_LOG_BLOCK();

    std::wstring bucketRegion{ gBucketCache.getBucketRegion(CONT_CALLER bucketName) };

    //traceW(L"bucketName: %s", bucketName.c_str());

    if (bucketRegion.empty())
    {
        // キャッシュに存在しない

        //traceW(L"do GetBucketLocation()");

        namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

        Aws::S3::Model::GetBucketLocationRequest request;
        request.SetBucket(WC2MB(bucketName));

        const auto outcome = mClient->GetBucketLocation(request);
        if (outcomeIsSuccess(outcome))
        {
            // ロケーションが取得できたとき

            const auto& result = outcome.GetResult();
            const auto& location = result.GetLocationConstraint();

            bucketRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

            //traceW(L"success, region is %s", bucketRegion.c_str());
        }
        
        if (bucketRegion.empty())
        {
            // 取得できないときを含めて、値がないときはデフォルト値にする

            bucketRegion = MB2WC(AWS_DEFAULT_REGION);

            //traceW(L"error, fall back region is %s", bucketRegion.c_str());
        }

        gBucketCache.addBucketRegion(CONT_CALLER bucketName, bucketRegion);
    }
    else
    {
        //traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }

    return bucketRegion;
}

bool AwsS3::unsafeHeadBucket(CALLER_ARG const std::wstring& argBucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    //traceW(L"bucket: %s", bucketName.c_str());

    if (!isInBucketFilters(argBucketName))
    {
        // バケットフィルタに合致しない

        traceW(L"%s: is not in filters, skip", argBucketName.c_str());

        return false;
    }

    // キャッシュから探す

    const auto bucket{ gBucketCache.find(CONT_CALLER argBucketName) };
    if (!bucket)
    {
        // キャッシュに見つからない

        traceW(L"not found");
        return false;
    }

    const std::wstring bucketRegion{ this->getBucketLocation(CONT_CALLER argBucketName) };
    if (bucketRegion != mRegion)
    {
        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

        return false;
    }

    if (pFileInfo)
    {
        NTSTATUS ntstatus = GetFileInfoInternal(mRefDir.handle(), pFileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return false;
        }
    }

    //traceW(L"success");

    return true;
}

bool AwsS3::unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */, const std::vector<std::wstring>& options)
{
    NEW_LOG_BLOCK();

    DirInfoListType dirInfoList;

    if (gBucketCache.empty(CONT_CALLER0))
    {
        const auto now{ std::chrono::system_clock::now() };
        const auto lastSetTime{ gBucketCache.getLastSetTime(CONT_CALLER0) };
        const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastSetTime);

        if (elapsed.count() < mConfig.bucketCacheExpiryMin)
        {
            // バケット一覧が空である状況のキャッシュ有効期限内

            traceW(L"empty buckets, short time cache");
            return true;
        }

        //traceW(L"cache empty");

        // バケット一覧の取得

        Aws::S3::Model::ListBucketsRequest request;

        const auto outcome = mClient->ListBuckets(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: ListBuckets");
            return false;
        }

        const auto& result = outcome.GetResult();

        for (const auto& bucket : result.GetBuckets())
        {
            const auto bucketName{ MB2WC(bucket.GetName()) };

            if (!isInBucketFilters(bucketName))
            {
                // バケット名によるフィルタリング

                //traceW(L"%s: is not in filters, skip", bucketName.c_str());
                continue;
            }

            // バケットの作成日時を取得

            const auto creationMillis{ bucket.GetCreationDate().Millis() };
            traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMilliToLocalTimeStringW(creationMillis).c_str());

            const auto FileTime = UtcMillisToWinFileTime100ns(creationMillis);

            // ディレクトリ・エントリを生成

            auto dirInfo = makeDirInfo_dir(bucketName, FileTime);
            APP_ASSERT(dirInfo);

            // バケットは常に読み取り専用

            dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;

            dirInfoList.emplace_back(dirInfo);

            // 最大バケット表示数の確認

            if (mConfig.maxDisplayBuckets > 0)
            {
                if (dirInfoList.size() >= mConfig.maxDisplayBuckets)
                {
                    break;
                }
            }
        }

        //traceW(L"update cache");

        // キャッシュにコピー

        gBucketCache.set(CONT_CALLER dirInfoList);
    }
    else
    {
        // キャッシュからコピー

        dirInfoList = gBucketCache.get(CONT_CALLER0);

        //traceW(L"use cache: size=%zu", dirInfoList.size());
    }

    if (pDirInfoList)
    {
        for (auto it=dirInfoList.begin(); it!=dirInfoList.end(); )
        {
            const std::wstring bucketName{ (*it)->FileNameBuf };

            // リージョン・キャッシュから異なるリージョンであるか調べる

            const std::wstring bucketRegion{ gBucketCache.getBucketRegion(CONT_CALLER bucketName) };

            if (!bucketRegion.empty())
            {
                if (bucketRegion != mRegion)
                {
                    // リージョンが異なる場合は HIDDEN 属性を付与
                    //
                    // --> headBucket() でリージョンを取得しているので、バケット・キャッシュ作成時ではできない

                    (*it)->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }
            }

            if (!options.empty())
            {
                const auto itOpts{ std::find(options.begin(), options.end(), bucketName) };
                if (itOpts == options.end())
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

// -----------------------------------------------------------------------------------
//
// 外部から呼び出されるインターフェース
//

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//

static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)


bool AwsS3::headBucket(CALLER_ARG const std::wstring& argBucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headBucket);
    THREAD_SAFE();

    return this->unsafeHeadBucket(CONT_CALLER argBucketName, pFileInfo);
}

bool AwsS3::listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(listBuckets);
    THREAD_SAFE();

    return this->unsafeListBuckets(CONT_CALLER pDirInfoList, {});
}

void AwsS3::clearBucketCache(CALLER_ARG0)
{
    THREAD_SAFE();

    gBucketCache.clear(CONT_CALLER0);
}

void AwsS3::reportBucketCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();

    // キャッシュのレポート

    gBucketCache.report(CONT_CALLER fp);
}

bool AwsS3::reloadBucketCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    const auto lastSetTime = gBucketCache.getLastSetTime(CONT_CALLER0);

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

        gBucketCache.clear(CONT_CALLER0);

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