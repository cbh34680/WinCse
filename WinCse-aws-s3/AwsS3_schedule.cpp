#include "AwsS3.hpp"
#include <filesystem>

using namespace WCSE;


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
        // 1 ���Ԋu�� run() �����s

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

    // TimerTask ����Ăяo����A�������̌Â����̂��폜

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
        // 10 ���Ԋu�� run() �����s

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

    // IdleTask ����Ăяo����A��������t�@�C���̌Â����̂��폜

    const auto now{ std::chrono::system_clock::now() };

    // �o�P�b�g�E�L���b�V���̍č쐬

    this->reloadListBucketsCache(CONT_CALLER now - std::chrono::minutes(mConfig.bucketCacheExpiryMin));

    // �t�@�C���E�L���b�V��
    //
    // �ŏI�A�N�Z�X���������莞�Ԍo�߂����L���b�V���E�t�@�C�����폜����

    APP_ASSERT(std::filesystem::is_directory(mCacheDataDir));

    const auto duration = now.time_since_epoch();
    const UINT64 nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
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
    // �o�P�b�g�ꗗ�̐�ǂ�

    getWorker(L"delayed")->addTask(CONT_CALLER new ListBucketsTask{ this });

    // ������s�^�X�N��o�^

    getWorker(L"timer")->addTask(CONT_CALLER new TimerTask{ this });

    // �A�C�h�����̃^�X�N��o�^

    getWorker(L"timer")->addTask(CONT_CALLER new IdleTask{ this });
}

// EOF