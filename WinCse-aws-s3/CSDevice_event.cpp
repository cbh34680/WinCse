#include "CSDevice.hpp"
#include <filesystem>

using namespace WCSE;


void CSDevice::onTimer(CALLER_ARG0)
{
    //NEW_LOG_BLOCK();

    // TimerTask から呼び出され、メモリの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    const auto num = mQueryObject->deleteOldCache(
        CONT_CALLER now - std::chrono::minutes(mRuntimeEnv->ObjectCacheExpiryMin));

    if (num > 0)
    {
        NEW_LOG_BLOCK();

        traceW(L"delete %d records", num);

        //traceW(L"done.");
    }
}

void CSDevice::onIdle(CALLER_ARG0)
{
    //NEW_LOG_BLOCK();

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    // ファイル・キャッシュ
    //
    // 最終アクセス日時から一定時間経過したキャッシュ・ファイルを削除する

    APP_ASSERT(std::filesystem::is_directory(mRuntimeEnv->CacheDataDir));

    const auto duration = now.time_since_epoch();
    const UINT64 nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    const int cacheFileRetentionMin = mRuntimeEnv->CacheFileRetentionMin;

    forEachFiles(mRuntimeEnv->CacheDataDir, [this, nowMillis, cacheFileRetentionMin](const auto& wfd, const auto& fullPath)
    {
        const auto fileMillis = WinFileTimeToUtcMillis(wfd.ftLastAccessTime);
        const auto diffMillis = nowMillis - fileMillis;

        if (diffMillis > (TIMEMILLIS_1MINull * cacheFileRetentionMin))
        {
            NEW_LOG_BLOCK();

            traceW(L"cache file=%s nowMillis=%llu fileMillis=%llu diffMillis=%llu cacheFileRetentionMin=%d",
                wfd.cFileName, nowMillis, fileMillis, diffMillis, cacheFileRetentionMin);

            if (::DeleteFilePassively(fullPath.c_str()))
            {
                traceW(L"--> Removed");
            }
            else
            {
                const auto lerr = ::GetLastError();
                traceW(L"--> Remove error, lerr=%lu", lerr);
            }
        }
    });

    // ファイル・キャッシュのディレクトリ
    // 上記でファイルが削除され、空になったディレクトリは削除

    forEachDirs(mRuntimeEnv->CacheDataDir, [this, nowMillis, cacheFileRetentionMin](const auto& wfd, const auto& fullPath)
    {
        const auto fileMillis = WinFileTimeToUtcMillis(wfd.ftLastAccessTime);
        const auto diffMillis = nowMillis - fileMillis;

        if (diffMillis > (TIMEMILLIS_1MINull * cacheFileRetentionMin))
        {
            NEW_LOG_BLOCK();

            traceW(L"cache dir=%s nowMillis=%llu fileMillis=%llu diffMillis=%llu cacheFileRetentionMin=%d",
                wfd.cFileName, nowMillis, fileMillis, diffMillis, cacheFileRetentionMin);

            std::error_code ec;
            std::filesystem::remove(fullPath, ec);

            if (ec)
            {
                const auto lerr = ::GetLastError();

                traceW(L"--> Remove error, lerr=%lu", lerr);
            }
            else
            {
                traceW(L"--> Removed");
            }
        }
    });

    //traceW(L"done.");

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    // バケット・キャッシュの再作成

    this->reloadBuckets(CONT_CALLER now - std::chrono::minutes(mRuntimeEnv->BucketCacheExpiryMin));
}

void CSDevice::onNotif(CALLER_ARG DWORD argEventId, PCWSTR argEventName)
{
    NEW_LOG_BLOCK();

    switch (argEventId)
    {
        case 0:
        {
            //
            // 各種情報のレポートを出力
            //
            SYSTEMTIME st;
            ::GetLocalTime(&st);

            std::wostringstream ss;

            ss << mRuntimeEnv->CacheReportDir;
            ss << L'\\';
            ss << L"report";
            ss << L'-';
            ss << std::setw(4) << std::setfill(L'0') << st.wYear;
            ss << std::setw(2) << std::setfill(L'0') << st.wMonth;
            ss << std::setw(2) << std::setfill(L'0') << st.wDay;
            ss << L'-';
            ss << std::setw(2) << std::setfill(L'0') << st.wHour;
            ss << std::setw(2) << std::setfill(L'0') << st.wMinute;
            ss << std::setw(2) << std::setfill(L'0') << st.wSecond;
            ss << L".log";

            const auto path{ ss.str() };

            FILE* fp = nullptr;
            if (_wfopen_s(&fp, path.c_str(), L"wt") == 0)
            {
                DWORD handleCount = 0;
                if (GetProcessHandleCount(GetCurrentProcess(), &handleCount))
                {
                    fwprintf(fp, L"ProcessHandle=%lu\n", handleCount);
                }

                fwprintf(fp, L"[ListBucketsCache]\n");
                mQueryBucket->reportCache(START_CALLER fp);

                fwprintf(fp, L"[ObjectCache]\n");
                mQueryObject->reportCache(START_CALLER fp);

                fclose(fp);
                fp = nullptr;

                traceW(L">>>>> REPORT OUTPUT=%s <<<<<", path.c_str());
            }

            break;
        }

        case 1:
        {
            mQueryBucket->clearCache(START_CALLER0);
            mQueryObject->clearCache(START_CALLER0);

            this->onTimer(START_CALLER0);
            this->onIdle(START_CALLER0);

            //this->reloadBuckets(START_CALLER std::chrono::system_clock::now());

            traceW(L">>>>> CACHE CLEAN <<<<<");

            break;
        }
    }
}

// EOF