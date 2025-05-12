#include "CSDriverBase.hpp"

using namespace CSELIB;
using namespace CSEDRV;


void CSDriverBase::onIdle()
{
    NEW_LOG_BLOCK();

    if (!std::filesystem::is_directory(mRuntimeEnv->CacheDataDir))
    {
        traceW(L"fault: not directory CacheDataDir=%s", mRuntimeEnv->CacheDataDir.c_str());
        return;
    }

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    // ファイル・キャッシュ
    //
    // 最終アクセス日時から一定時間経過したキャッシュ・ファイルを削除する


    const auto duration = now.time_since_epoch();
    const auto nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    traceW(L"now=%lld %s", nowMillis, UtcMillisToLocalTimeStringW(nowMillis).c_str());

    const auto cacheFileRetentionMillis = TIMEMILLIS_1MINull * mRuntimeEnv->CacheFileRetentionMin;

    forEachFiles(mRuntimeEnv->CacheDataDir, [this, nowMillis, cacheFileRetentionMillis](const auto& wfd, const auto& fullPath)
    {
        NEW_LOG_BLOCK();

        const auto fileMillis = WinFileTimeToUtcMillis(wfd.ftLastAccessTime);
        const auto diffMillis = nowMillis - fileMillis;

        traceW(L"cache file=%s fileMillis=%llu %s", wfd.cFileName, fileMillis, UtcMillisToLocalTimeStringW(fileMillis).c_str());
        traceW(L"diffMillis=%llu cacheFileRetentionMillis=%llu", diffMillis, cacheFileRetentionMillis);

        if (diffMillis > cacheFileRetentionMillis)
        {
            traceW(L"The cache file has expired.");

            if (::DeleteFilePassively(fullPath.c_str()))
            {
                traceW(L"--> Removed");
            }
            else
            {
                const auto lerr = ::GetLastError();
                errorW(L"fault: Remove error lerr=%lu", lerr);
            }
        }
        else
        {
            traceW(L"The cache file is still valid.");
        }
    });

    // ファイル・キャッシュのディレクトリ
    // 上記でファイルが削除され、空になったディレクトリは削除

    forEachDirs(mRuntimeEnv->CacheDataDir, [this, nowMillis, cacheFileRetentionMillis](const auto& wfd, const auto& fullPath)
    {
        NEW_LOG_BLOCK();

        const auto fileMillis = WinFileTimeToUtcMillis(wfd.ftCreationTime);
        const auto diffMillis = nowMillis - fileMillis;

        traceW(L"cache directory=%s fileMillis=%llu %s", wfd.cFileName, fileMillis, UtcMillisToLocalTimeStringW(fileMillis).c_str());
        traceW(L"diffMillis=%llu cacheFileRetentionMillis=%llu", diffMillis, cacheFileRetentionMillis);

        if (diffMillis > cacheFileRetentionMillis)
        {
            traceW(L"The cache directory has expired.");

            std::error_code ec;
            std::filesystem::remove(fullPath, ec);

            if (ec)
            {
                const auto lerr = ::GetLastError();

                errorA("fault: Remove error lerr=%lu ec=%s", lerr, ec.message().c_str());
            }
            else
            {
                traceW(L"--> Removed");
            }
        }
        else
        {
            traceW(L"The cache directory is still valid.");
        }
    });

    traceW(L"done.");
}

bool CSDriverBase::onNotif(const std::wstring& argNotifName)
{
    NEW_LOG_BLOCK();

    if (argNotifName == L"Global\\WinCse-util-print-report")
    {
        //
        // 各種情報のレポートを出力
        //
        SYSTEMTIME st;
        ::GetLocalTime(&st);

        std::wostringstream ss;

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
        
        const auto path{ (mRuntimeEnv->CacheReportDir / ss.str()).wstring() };

        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"wt") == 0)
        {
            DWORD handleCount = 0;
            if (GetProcessHandleCount(GetCurrentProcess(), &handleCount))
            {
                fwprintf(fp, L"ProcessHandle=%lu\n", handleCount);
            }

            mDevice->printReport(fp);

            fclose(fp);
            fp = nullptr;

            traceW(L">>>>> REPORT OUTPUT=%s <<<<<", path.c_str());
        }

        return true;
    }

    return false;
}

// EOF