#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <cinttypes>

using namespace WinCseLib;


std::wstring AwsS3::unsafeGetBucketRegion(CALLER_ARG const std::wstring& bucketName)
{
    NEW_LOG_BLOCK();

    std::wstring bucketRegion;

    traceW(L"bucketName: %s", bucketName.c_str());

    if (mBucketCache.findRegion(CONT_CALLER bucketName, &bucketRegion))
    {
        traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }
    else
    {
        // キャッシュに存在しない

        traceW(L"do GetBucketLocation()");

        namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

        Aws::S3::Model::GetBucketLocationRequest request;
        request.SetBucket(WC2MB(bucketName).c_str());

        const auto outcome = mClient.ptr->GetBucketLocation(request);
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

        mBucketCache.updateRegion(CONT_CALLER bucketName, bucketRegion);
    }

    return bucketRegion;
}

static std::mutex gGuard;

#define THREAD_SAFE() \
    std::lock_guard<std::mutex> lock_(gGuard)

bool AwsS3::headBucket(CALLER_ARG const std::wstring& bucketName)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(!bucketName.empty());
    APP_ASSERT(bucketName.back() != L'/');

    traceW(L"bucket: %s", bucketName.c_str());

    if (!isInBucketFiltersW(bucketName))
    {
        // バケットフィルタに合致しない
        traceW(L"%s: is not in filters, skip", bucketName.c_str());

        return false;
    }

    // キャッシュから探す
    const auto bucket{ mBucketCache.find(CONT_CALLER bucketName) };
    if (bucket)
    {
        traceW(L"hit in buckets cache");
    }
    else
    {
        traceW(L"warn: no match");

        Aws::S3::Model::HeadBucketRequest request;
        request.SetBucket(WC2MB(bucketName).c_str());

        const auto outcome = mClient.ptr->HeadBucket(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: HeadBucket");
            return false;
        }
    }

    const std::wstring bucketRegion{ this->unsafeGetBucketRegion(CONT_CALLER bucketName) };
    if (bucketRegion != mRegion)
    {
        // バケットのリージョンが異なるので拒否

        traceW(L"%s: no match bucket-region", bucketRegion.c_str());
        return false;
    }

    traceW(L"success");

    return true;
}

bool AwsS3::listBuckets(CALLER_ARG
    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>>* pDirInfoList,
    const std::vector<std::wstring>& options)
{
    THREAD_SAFE();
    NEW_LOG_BLOCK();

    std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> dirInfoList;

    if (mBucketCache.empty(CONT_CALLER0))
    {
        traceW(L"cache empty");

        // バケット一覧の取得

        Aws::S3::Model::ListBucketsRequest request;

        const auto outcome = mClient.ptr->ListBuckets(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: ListBuckets");
            return false;
        }

        const auto& result = outcome.GetResult();

        for (const auto& bucket : result.GetBuckets())
        {
            const std::wstring bucketName{ MB2WC(bucket.GetName()) };

            if (!isInBucketFiltersW(bucketName))
            {
                // バケット名によるフィルタリング

                traceW(L"%s: is not in filters, skip", bucketName.c_str());
                continue;
            }

            std::wstring bucketRegion;
            if (mBucketCache.findRegion(CONT_CALLER bucketName, &bucketRegion))
            {
                // 異なるリージョンのバケットは無視

                if (bucketRegion != mRegion)
                {
                    traceW(L"%s: no match region, skip", bucketRegion.c_str());
                    continue;
                }
            }

            auto dirInfo = mallocDirInfoW(bucketName, L"");
            APP_ASSERT(dirInfo);

            const auto FileTime = UtcMillisToWinFileTimeIn100ns(bucket.GetCreationDate().Millis());

            dirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;
            dirInfo->FileInfo.CreationTime = FileTime;
            dirInfo->FileInfo.LastAccessTime = FileTime;
            dirInfo->FileInfo.LastWriteTime = FileTime;
            dirInfo->FileInfo.ChangeTime = FileTime;

            dirInfoList.push_back(dirInfo);

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
        mBucketCache.save(CONT_CALLER dirInfoList);
    }
    else
    {
        // キャッシュからコピー
        mBucketCache.load(CONT_CALLER mRegion, dirInfoList);

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

            std::vector<std::shared_ptr<FSP_FSCTL_DIR_INFO>> optsDirInfoList;

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