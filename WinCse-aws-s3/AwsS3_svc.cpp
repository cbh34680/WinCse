#include "AwsS3.hpp"
#include <iomanip>
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

        traceW(L"call ListBuckets");

        mAwsS3->listBuckets(CONT_CALLER nullptr, {});
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
    NEW_LOG_BLOCK();

    // TimerTask から呼び出され、メモリの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    const auto numDelete = this->deleteOldObjects(CONT_CALLER now - std::chrono::minutes(3));
    traceW(L"delete %d records", numDelete);

    traceW(L"done.");
}

struct IdleTask : public IScheduledTask
{
    AwsS3* mAwsS3;

    IdleTask(AwsS3* argAwsS3) : mAwsS3(argAwsS3) { }

    bool shouldRun(int i) const noexcept override
    {
        // 30 分間隔で run() を実行

        return i % 30 == 0;
    }

    void run(CALLER_ARG0) override
    {
        mAwsS3->onIdle(CONT_CALLER0);
    }
};

void AwsS3::onIdle(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    // バケット・キャッシュの再作成

    this->reloadBukcetsIfNecessary(CONT_CALLER now - std::chrono::minutes(20));

    // ファイル・キャッシュ
    //
    // 最終アクセス日時から 6 時間以上経過したキャッシュ・ファイルを削除する

    APP_ASSERT(std::filesystem::is_directory(mCacheDataDir));

    const auto duration = now.time_since_epoch();
    const uint64_t nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    forEachFiles(mCacheDataDir, [this, nowMillis, &LOG_BLOCK()](const auto& wfd, const auto& fullPath)
    {
        APP_ASSERT(!FA_IS_DIR(wfd.dwFileAttributes));

        const auto lastAccessTime = WinFileTimeToUtcMillis(wfd.ftLastAccessTime);
        const auto diffMillis = nowMillis - lastAccessTime;

        traceW(L"cache file=\"%s\" nowMillis=%llu lastAccessTime=%llu diffMillis=%llu",
            wfd.cFileName, nowMillis, lastAccessTime, diffMillis);

        if (diffMillis > TIMEMILLIS_1HOURull * 6)
        {
            if (::DeleteFile(fullPath.c_str()))
            {
                traceW(L"%s: removed", fullPath.c_str());
            }
            else
            {
                const auto lerr = ::GetLastError();
                traceW(L"%s: remove error, lerr=%lu", fullPath.c_str(), lerr);
            }
        }
    });

    traceW(L"done.");
}

static std::atomic<bool> gEndWorkerFlag;
static std::thread* gNotifWorker;

static HANDLE gNotifEvents[2];
static const wchar_t* gEventNames[] =
{
    L"Global\\WinCse-AwsS3-util-print-report",
    L"Global\\WinCse-AwsS3-util-clear-cache",
};

static const int gNumNotifEvents = _countof(gNotifEvents);

bool AwsS3::OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    StatsIncr(OnSvcStart);

    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    APP_ASSERT(FileSystem);

    bool ret = false;

    mFileSystem = FileSystem;

    // バケット一覧の先読み

    getWorker(L"delayed")->addTask(START_CALLER new ListBucketsTask{ this });

    // 定期実行タスクを登録

    getWorker(L"timer")->addTask(START_CALLER new TimerTask{ this });

    // アイドル時のタスクを登録

    getWorker(L"idle")->addTask(START_CALLER new IdleTask{ this });

    // 外部からの通知待ちイベントの生成

    SECURITY_ATTRIBUTES sa{ 0 };
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);

    // セキュリティ記述子の作成

    PSECURITY_DESCRIPTOR pSD = (PSECURITY_DESCRIPTOR)::LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!pSD)
    {
        traceW(L"fault: LocalAlloc");
        goto exit;
    }

    if (!::InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION))
    {
        traceW(L"fault: InitializeSecurityDescriptor");
        goto exit;
    }

    // すべてのユーザーにフルアクセスを許可
#pragma warning(suppress: 6248)
    if (!::SetSecurityDescriptorDacl(pSD, TRUE, NULL, FALSE))
    {
        traceW(L"fault: SetSecurityDescriptorDacl");
        goto exit;
    }

    sa.lpSecurityDescriptor = pSD;

    gEndWorkerFlag = false;

    static_assert(_countof(gEventNames) == gNumNotifEvents);

    for (int i=0; i<gNumNotifEvents; i++)
    {
        gNotifEvents[i] = ::CreateEventW(&sa, FALSE, FALSE, gEventNames[i]);
        if (!gNotifEvents[i])
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: CreateEvent(%s) error=%ld", gEventNames[i], lerr);
            return false;
        }
    }

    gNotifWorker = new std::thread(&AwsS3::notifListener, this);
    APP_ASSERT(gNotifWorker);

    ::SetThreadDescription(gNotifWorker->native_handle(), L"WinCse::notifListener");

    ret = true;

exit:
    if (pSD)
    {
        ::LocalFree(pSD);
        pSD = NULL;
    }

    return ret;
}

void AwsS3::OnSvcStop()
{
    StatsIncr(OnSvcStop);

    NEW_LOG_BLOCK();

    // デストラクタからも呼ばれるので、再入可能としておくこと

    gEndWorkerFlag = true;

    for (int i=0; i<gNumNotifEvents; i++)
    {
        if (gNotifEvents[i])
        {
            const auto b = ::SetEvent(gNotifEvents[i]);
            APP_ASSERT(b);
        }
    }

    if (gNotifWorker)
    {
        gNotifWorker->join();

        delete gNotifWorker;
        gNotifWorker = nullptr;
    }

    for (int i=0; i<gNumNotifEvents; i++)
    {
        if (gNotifEvents[i])
        {
            ::CloseHandle(gNotifEvents[i]);
            gNotifEvents[i] = NULL;
        }
    }

    // AWS S3 処理終了

    if (mSDKOptions)
    {
        traceW(L"aws shutdown");
        Aws::ShutdownAPI(*mSDKOptions);

        mSDKOptions.reset();
    }

    mRefFile.close();
    mRefDir.close();
}

void AwsS3::notifListener()
{
    NEW_LOG_BLOCK();

    while (true)
    {
        const auto reason = ::WaitForMultipleObjects(gNumNotifEvents, gNotifEvents, FALSE, INFINITE);

        if (WAIT_OBJECT_0 <= reason && reason < (WAIT_OBJECT_0 + gNumNotifEvents))
        {
            // go next
        }
        else
        {
            const auto lerr = ::GetLastError();
            traceW(L"un-expected reason=%lu, lerr=%lu, break", reason, lerr);
            break;
        }

        if (gEndWorkerFlag)
        {
            traceW(L"catch end-thread request, break");
            break;
        }

        switch (reason - WAIT_OBJECT_0)
        {
            case 0:     // print-report
            {
                //
                // 各種情報のレポートを出力
                //
                SYSTEMTIME st;
                ::GetLocalTime(&st);

                std::wstringstream ss;
                ss << mCacheReportDir;
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

                    fwprintf(fp, L"ClientPtr.RefCount=%d\n", mClient.getRefCount());

                    fwprintf(fp, L"[BucketCache]\n");
                    this->reportBucketCache(START_CALLER fp);

                    fwprintf(fp, L"[ObjectCache]\n");
                    this->reportObjectCache(START_CALLER fp);

                    fclose(fp);
                    fp = nullptr;
                }

                break;
            }

            case 1:     // clear-cache
            {
                clearObjects(START_CALLER0);

                //FspFileSystemStopDispatcher(mFileSystem);
                //FspFileSystemDelete(mFileSystem);
                //FspServiceStop(mWinFspService);

                break;
            }
        }
    }

    traceW(L"thread end");
}

// EOF