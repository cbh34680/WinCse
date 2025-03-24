#include "AwsS3.hpp"
#include "BucketCache.hpp"


using namespace WinCseLib;

extern BucketCache gBucketCache;


std::wstring AwsS3::unlockGetBucketRegion(CALLER_ARG const std::wstring& bucketName)
{
    NEW_LOG_BLOCK();

    std::wstring bucketRegion;

    traceW(L"bucketName: %s", bucketName.c_str());

    if (gBucketCache.findRegion(CONT_CALLER bucketName, &bucketRegion))
    {
        traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }
    else
    {
        // キャッシュに存在しない

        traceW(L"do GetBucketLocation()");

        namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

        Aws::S3::Model::GetBucketLocationRequest request;
        request.SetBucket(WC2MB(bucketName));

        const auto outcome = mClient->GetBucketLocation(request);
        if (outcomeIsSuccess(outcome))
        {
            const auto& result = outcome.GetResult();
            const auto& location = result.GetLocationConstraint();

            bucketRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

            traceW(L"success, region is %s", bucketRegion.c_str());
        }
        
        if (bucketRegion.empty())
        {
            bucketRegion = MB2WC(AWS_DEFAULT_REGION);

            traceW(L"error, fall back region is %s", bucketRegion.c_str());
        }

        gBucketCache.updateRegion(CONT_CALLER bucketName, bucketRegion);
    }

    return bucketRegion;
}

// -----------------------------------------------------------------------------------
//
// 外部から呼び出されるインターフェース
//

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
static std::mutex gGuard;
BucketCache gBucketCache;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

void AwsS3::clearBuckets(CALLER_ARG0)
{
    THREAD_SAFE();
    gBucketCache.clear(CONT_CALLER0);
}

void AwsS3::reloadBukcetsIfNeed(CALLER_ARG0)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    namespace chrono = std::chrono;
    const auto now { chrono::system_clock::now() };

    const auto lastSetTime = gBucketCache.getLastSetTime(CONT_CALLER0);

    if ((now - chrono::minutes(60)) > lastSetTime)
    {
        // バケット・キャッシュを作成してから 60 分以上経過
        traceW(L"need re-load");

        // バケットのキャッシュを削除して、再度一覧を取得する
        gBucketCache.clear(CONT_CALLER0);

        // バケット一覧の取得 --> キャッシュの生成
        listBuckets(CONT_CALLER nullptr, {});
    }
    else
    {
        traceW(L"is valid");
    }
}

void AwsS3::reportBucketCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();

    gBucketCache.report(CONT_CALLER fp);
}

struct NotifRemoveBucketTask : public ITask
{
    CanIgnoreDuplicates getCanIgnoreDuplicates() const noexcept override { return CanIgnoreDuplicates::Yes; }
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

bool AwsS3::headBucket(CALLER_ARG const std::wstring& bucketName)
{
    StatsIncr(headBucket);

    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!bucketName.empty());
    APP_ASSERT(bucketName.back() != L'/');

    traceW(L"bucket: %s", bucketName.c_str());

    if (!isInBucketFilters(bucketName))
    {
        // バケットフィルタに合致しない
        traceW(L"%s: is not in filters, skip", bucketName.c_str());

        return false;
    }

    // キャッシュから探す
    const auto bucket{ gBucketCache.find(CONT_CALLER bucketName) };
    if (bucket)
    {
        traceW(L"hit in buckets cache");
    }
    else
    {
        traceW(L"warn: no match");

        Aws::S3::Model::HeadBucketRequest request;
        request.SetBucket(WC2MB(bucketName));

        const auto outcome = mClient->HeadBucket(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: HeadBucket");
            return false;
        }
    }

    const std::wstring bucketRegion{ this->unlockGetBucketRegion(CONT_CALLER bucketName) };
    if (bucketRegion != mRegion)
    {
        // バケットのリージョンが異なるので拒否

        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

        // 非表示になるバケットについて WinFsp に通知
        getWorker(L"delayed")->addTask(START_CALLER new NotifRemoveBucketTask{ mFileSystem, std::wstring(L"\\") + bucketName });

        return false;
    }

    traceW(L"success");

    return true;
}

bool AwsS3::listBuckets(CALLER_ARG DirInfoListType* pDirInfoList /* nullable */,
    const std::vector<std::wstring>& options)
{
    StatsIncr(listBuckets);

    THREAD_SAFE();
    NEW_LOG_BLOCK();

    DirInfoListType dirInfoList;

    if (gBucketCache.empty(CONT_CALLER0))
    {
        traceW(L"cache empty");

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

                traceW(L"%s: is not in filters, skip", bucketName.c_str());
                continue;
            }

            std::wstring bucketRegion;
            if (gBucketCache.findRegion(CONT_CALLER bucketName, &bucketRegion))
            {
                // 異なるリージョンのバケットは無視

                if (bucketRegion != mRegion)
                {
                    traceW(L"%s: no match region, skip", bucketRegion.c_str());
                    continue;
                }
            }

            const auto creationMillis{ bucket.GetCreationDate().Millis() };
            traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMilliToLocalTimeStringW(creationMillis).c_str());

            const auto FileTime = UtcMillisToWinFileTime100ns(creationMillis);

            // バケット一覧なので、他の場所と異なりバケット名をキーにする

            auto dirInfo = makeDirInfo_dir(ObjectKey{ L"", bucketName }, FileTime);
            APP_ASSERT(dirInfo);

            // バケットは常に読み取り専用

            dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;

            dirInfoList.emplace_back(dirInfo);

            if (mMaxBuckets > 0)
            {
                if (dirInfoList.size() >= mMaxBuckets)
                {
                    break;
                }
            }
        }

        traceW(L"update cache");

        // キャッシュにコピー
        gBucketCache.save(CONT_CALLER dirInfoList);
    }
    else
    {
        // キャッシュからコピー
        gBucketCache.load(CONT_CALLER mRegion, dirInfoList);

        traceW(L"use cache: size=%zu", dirInfoList.size());
    }

    bool ret = false;

    if (pDirInfoList)
    {
        if (options.empty())
        {
            // 抽出条件がないので、全て提供

            *pDirInfoList = std::move(dirInfoList);
            ret = true;
        }
        else
        {
            // 抽出条件に一致するものを提供

            DirInfoListType optsDirInfoList;

            for (const auto& dirInfo : dirInfoList)
            {
                const std::wstring bucketName{ dirInfo->FileNameBuf };
                const auto it = std::find(options.begin(), options.end(), bucketName);

                if (it != options.end())
                {
                    optsDirInfoList.push_back(dirInfo);
                }

                if (optsDirInfoList.size() == options.size())
                {
                    break;
                }
            }

            if (!optsDirInfoList.empty())
            {
                *pDirInfoList = std::move(optsDirInfoList);
                ret = true;
            }
        }
    }
    else
    {
        ret = !dirInfoList.empty();
    }

    return ret;
}

// EOF