#include "AwsS3.hpp"
#include <filesystem>

using namespace WinCseLib;


struct ListBucketsTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::Yes; }
    Priority getPriority() const noexcept override { return Priority::Low; }

    AwsS3* mAwsS3;

    ListBucketsTask(AwsS3* argAwsS3) : mAwsS3(argAwsS3) { }

    std::wstring synonymString() const noexcept override
    {
        return L"ListBucketsTask";
    }

    void run(CALLER_ARG0) override
    {
        NEW_LOG_BLOCK();

        //traceW(L"call ListBuckets");

        mAwsS3->listBuckets(CONT_CALLER nullptr);
    }
};

struct TimerTask : public IScheduledTask
{
    AwsS3* mAwsS3;

    TimerTask(AwsS3* argAwsS3) : mAwsS3(argAwsS3) { }

    bool shouldRun(int i) const noexcept override
    {
        // 1 分間隔で run() を実行

        return true;
    }

    void run(CALLER_ARG0) override
    {
        mAwsS3->onTimer(CONT_CALLER0);
    }
};

void AwsS3::onTimer(CALLER_ARG0)
{
    //NEW_LOG_BLOCK();

    // TimerTask から呼び出され、メモリの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    const auto numDelete = this->deleteOldObjectCache(CONT_CALLER now - std::chrono::minutes(mConfig.objectCacheExpiryMin));
    if (numDelete)
    {
        NEW_LOG_BLOCK();

        traceW(L"delete %d records", numDelete);

        //traceW(L"done.");
    }
}

struct IdleTask : public IScheduledTask
{
    AwsS3* mAwsS3;

    IdleTask(AwsS3* argAwsS3) : mAwsS3(argAwsS3) { }

    bool shouldRun(int i) const noexcept override
    {
        // 10 分間隔で run() を実行

        return i % 10 == 0;
    }

    void run(CALLER_ARG0) override
    {
        mAwsS3->onIdle(CONT_CALLER0);
    }
};

void AwsS3::onIdle(CALLER_ARG0)
{
    //NEW_LOG_BLOCK();

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    // バケット・キャッシュの再作成

    this->reloadBucketCache(CONT_CALLER now - std::chrono::minutes(mConfig.bucketCacheExpiryMin));

    // ファイル・キャッシュ
    //
    // 最終アクセス日時から一定時間経過したキャッシュ・ファイルを削除する

    APP_ASSERT(std::filesystem::is_directory(mCacheDataDir));

    const auto duration = now.time_since_epoch();
    const uint64_t nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    const int cacheFileRetentionMin = mConfig.cacheFileRetentionMin;

    //forEachFiles(mCacheDataDir, [this, &LOG_BLOCK(), nowMillis, cacheFileRetentionMin](const auto& wfd, const auto& fullPath)
    forEachFiles(mCacheDataDir, [this, nowMillis, cacheFileRetentionMin](const auto& wfd, const auto& fullPath)
    {
        APP_ASSERT(!FA_IS_DIR(wfd.dwFileAttributes));

        const auto lastAccessTime = WinFileTimeToUtcMillis(wfd.ftLastAccessTime);
        const auto diffMillis = nowMillis - lastAccessTime;

        //traceW(L"cache file=%s nowMillis=%llu lastAccessTime=%llu diffMillis=%llu cacheFileRetentionMin=%d", wfd.cFileName, nowMillis, lastAccessTime, diffMillis, cacheFileRetentionMin);

        if (diffMillis > (TIMEMILLIS_1MINull * cacheFileRetentionMin))
        {
            NEW_LOG_BLOCK();

            traceW(L"cache file=%s nowMillis=%llu lastAccessTime=%llu diffMillis=%llu cacheFileRetentionMin=%d", wfd.cFileName, nowMillis, lastAccessTime, diffMillis, cacheFileRetentionMin);

            if (::DeleteFile(fullPath.c_str()))
            {
                traceW(L"--> Removed");
            }
            else
            {
                const auto lerr = ::GetLastError();
                traceW(L"--> Remove error, lerr=%lu", lerr);
            }
        }
        else
        {
            //traceW(L"--> Not necessary");
        }
    });

    //traceW(L"done.");
}

void AwsS3::addTasks(CALLER_ARG0)
{
    // バケット一覧の先読み

    getWorker(L"delayed")->addTask(CONT_CALLER new ListBucketsTask{ this });

    // 定期実行タスクを登録

    getWorker(L"timer")->addTask(CONT_CALLER new TimerTask{ this });

    // アイドル時のタスクを登録

    getWorker(L"idle")->addTask(CONT_CALLER new IdleTask{ this });
}

// EOF