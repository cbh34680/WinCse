#include "AwsS3B.hpp"
#include <filesystem>
#include <regex>

using namespace WCSE;


AwsS3B::AwsS3B(const std::wstring& argTempDir, const std::wstring& argIniSection,
    std::unordered_map<std::wstring, IWorker*>&& argWorkers)
    :
    mTempDir(argTempDir),
    mIniSection(argIniSection),
    mWorkers(std::move(argWorkers))
{
    APP_ASSERT(std::filesystem::exists(argTempDir));
    APP_ASSERT(std::filesystem::is_directory(argTempDir));

    mStats = &mStats_;
}

NTSTATUS AwsS3B::PreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argWorkDir);

    // 読み取り専用

    if (VolumeParams->ReadOnlyVolume)
    {
        mDefaultFileAttributes |= FILE_ATTRIBUTE_READONLY;
    }

    mWinFspService = Service;

    return STATUS_SUCCESS;
}

struct TimerTask : public IScheduledTask
{
    AwsS3B* mAwsS3B;

    TimerTask(AwsS3B* argAwsS3B) : mAwsS3B(argAwsS3B) { }

    bool shouldRun(int) const noexcept override
    {
        // 1 分間隔で run() を実行

        return true;
    }

    void run(CALLER_ARG0) override
    {
        mAwsS3B->onTimer(CONT_CALLER0);
    }
};

struct IdleTask : public IScheduledTask
{
    AwsS3B* mAwsS3B;

    IdleTask(AwsS3B* argAwsS3B) : mAwsS3B(argAwsS3B) { }

    bool shouldRun(int i) const noexcept override
    {
        // 10 分間隔で run() を実行

        return i % 10 == 0;
    }

    void run(CALLER_ARG0) override
    {
        mAwsS3B->onIdle(CONT_CALLER0);
    }
};

static PCWSTR CONFIGFILE_FNAME = L"WinCse.conf";

NTSTATUS AwsS3B::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    APP_ASSERT(FileSystem);

    namespace fs = std::filesystem;

    const std::wstring workDir{ argWorkDir };
    const auto confPath{ workDir + L'\\' + CONFIGFILE_FNAME };


    // 属性参照用ファイル/ディレクトリの準備

    mRefFile = ::CreateFileW
    (
        confPath.c_str(),
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,		// 共有モード
        NULL,														// セキュリティ属性
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL														// テンプレートなし
    );

    if (mRefFile.invalid())
    {
        traceW(L"fault: CreateFileW, confPath=%s", confPath.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    mRefDir = ::CreateFileW
    (
        argWorkDir,
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,     // 共有モード
        NULL,                                                       // セキュリティ属性
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL                                                        // テンプレートなし
    );

    if (mRefDir.invalid())
    {
        traceW(L"fault: CreateFileW, argWorkDir=%s", argWorkDir);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // 調整パラメータ

    const auto iniSection = mIniSection.c_str();
    const auto confPathCstr = confPath.c_str();

    mSettings = std::make_unique<Settings>(
        GetIniIntW(confPathCstr, iniSection, L"max_display_buckets",         8,   0, INT_MAX - 1),
        GetIniIntW(confPathCstr, iniSection, L"max_display_objects",      1000,   0, INT_MAX - 1),
        GetIniIntW(confPathCstr, iniSection, L"bucket_cache_expiry_min",    20,   1,        1440),
        GetIniIntW(confPathCstr, iniSection, L"object_cache_expiry_min",     3,   1,          60),
        GetIniIntW(confPathCstr, iniSection, L"cache_file_retention_min",   60,   1,       10080),
        ::GetPrivateProfileIntW(iniSection,  L"delete_after_upload",         0, confPathCstr) != 0,
        ::GetPrivateProfileIntW(iniSection,  L"strict_file_timestamp",       0, confPathCstr) != 0
    );

    mFileSystem = FileSystem;
    mWorkDir = workDir;
    mWorkDirCTime = STCTimeToWinFileTimeW(workDir);
    mConfPath = confPath;

    // 定期実行タスクを登録

    getWorker(L"timer")->addTask(START_CALLER new TimerTask{ this });

    // アイドル時のタスクを登録

    getWorker(L"timer")->addTask(START_CALLER new IdleTask{ this });

    // 外部からの通知待ちイベントの生成

    if (!this->setupNotifListener(START_CALLER0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

DirInfoType AwsS3B::makeDirInfo_attr(const std::wstring& argFileName, UINT64 argFileTime, UINT32 argFileAttributes)
{
    APP_ASSERT(!argFileName.empty());

    auto dirInfo = makeDirInfo(argFileName);
    APP_ASSERT(dirInfo);

    UINT32 fileAttributes = argFileAttributes | mDefaultFileAttributes;

    if (argFileName != L"." && argFileName != L".." && argFileName[0] == L'.')
    {
        // 隠しファイル

        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    dirInfo->FileInfo.FileAttributes = fileAttributes;

    dirInfo->FileInfo.CreationTime = argFileTime;
    dirInfo->FileInfo.LastAccessTime = argFileTime;
    dirInfo->FileInfo.LastWriteTime = argFileTime;
    dirInfo->FileInfo.ChangeTime = argFileTime;

    return dirInfo;
}

DirInfoType AwsS3B::makeDirInfo_byName(const std::wstring& argFileName, UINT64 argFileTime)
{
    APP_ASSERT(!argFileName.empty());

    const auto lastChar = argFileName[argFileName.length() - 1];

    return makeDirInfo_attr(argFileName, argFileTime, lastChar == L'/' ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL);
}

DirInfoType AwsS3B::makeDirInfo_dir(const std::wstring& argFileName, UINT64 argFileTime)
{
    return makeDirInfo_attr(argFileName, argFileTime, FILE_ATTRIBUTE_DIRECTORY);
}

static std::atomic<bool> gEndWorkerFlag;
static std::thread* gNotifWorker;

static HANDLE gNotifEvents[2];
static PCWSTR gEventNames[] =
{
    L"Global\\WinCse-AwsS3-util-print-report",
    L"Global\\WinCse-AwsS3-util-clear-cache",
};

static const int gNumNotifEvents = _countof(gNotifEvents);

bool AwsS3B::setupNotifListener(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);

    // セキュリティ記述子の作成

    SECURITY_DESCRIPTOR sd{};
    if (!::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
    {
        traceW(L"fault: InitializeSecurityDescriptor");
        return false;
    }

    // すべてのユーザーにフルアクセスを許可
#pragma warning(suppress: 6248)
    if (!::SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE))
    {
        traceW(L"fault: SetSecurityDescriptorDacl");
        return false;
    }

    sa.lpSecurityDescriptor = &sd;

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

    gNotifWorker = new std::thread(&AwsS3B::notifListener, this);
    APP_ASSERT(gNotifWorker);

    const auto hresult = ::SetThreadDescription(gNotifWorker->native_handle(), L"WinCse::notifListener");
    APP_ASSERT(SUCCEEDED(hresult));

    return true;
}

void AwsS3B::notifListener()
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

        const auto eventId = reason - WAIT_OBJECT_0;
        const auto eventName = gEventNames[eventId];

        traceW(L"call onNotifEvent eventId=%lu, eventName=%s", eventId, eventName);

        try
        {
            this->onNotifEvent(START_CALLER eventId, eventName);
        }
        catch (const std::exception& ex)
        {
            traceA("what: %s", ex.what());
            break;
        }
        catch (...)
        {
            traceA("unknown error, continue");
        }
    }

    traceW(L"thread end");
}

VOID AwsS3B::OnSvcStop()
{
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
}

// EOF