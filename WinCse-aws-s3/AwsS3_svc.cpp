#include "AwsS3.hpp"
#include <iomanip>
//#include <filesystem>

using namespace WCSE;


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

    // 長くなったので、外だし

    addTasks(START_CALLER0);

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
        traceW(L"join thread");
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

    //mRefFile.close();
    //mRefDir.close();
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

                    traceW(L">>>>> REPORT OUTPUT=%s <<<<<", path.c_str());
                }

                break;
            }

            case 1:     // clear-cache
            {
                clearBucketCache(START_CALLER0);
                clearObjectCache(START_CALLER0);
                onIdle(START_CALLER0);
                onTimer(START_CALLER0);

                //reloadBucketCache(START_CALLER std::chrono::system_clock::now());

                traceW(L">>>>> CACHE CLEAN <<<<<");

                break;
            }
        }
    }

    traceW(L"thread end");
}

// EOF