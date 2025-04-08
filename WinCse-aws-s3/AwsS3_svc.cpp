#include "AwsS3.hpp"
#include <iomanip>
#include <filesystem>

using namespace WCSE;


static PCWSTR CONFIGFILE_FNAME = L"WinCse.conf";
static PCWSTR CACHE_DATA_DIR_FNAME = L"aws-s3\\cache\\data";
static PCWSTR CACHE_REPORT_DIR_FNAME = L"aws-s3\\cache\\report";


NTSTATUS AwsS3::PreCreateFilesystem(FSP_SERVICE *Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
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

static bool decryptIfNecessary(const std::string& secureKeyStr, std::string* pInOut)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pInOut);

    std::string str{ *pInOut };

    if (!str.empty())
    {
        if (str.length() > 8)
        {
            if (str.substr(0, 8) == "{aes256}")
            {
                // 先頭の "{aes256}" を除く

                const std::string concatB64Str{ str.substr(8) };

                // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する

                const std::vector<BYTE> aesKey{ secureKeyStr.begin(), secureKeyStr.end() };

                // BASE64 文字列をデコード

                std::string concatStr;
                if (!Base64DecodeA(concatB64Str, &concatStr))
                {
                    traceW(L"fault: Base64DecodeA");
                    return false;
                }

                const std::vector<BYTE> concatBytes{ concatStr.begin(), concatStr.end() };

                if (concatBytes.size() < 17)
                {
                    // IV + データなので最低でも 16 + 1 byte は必要

                    traceW(L"fault: concatBytes.size() < 17");
                    return false;
                }

                // 先頭の 16 byte が IV

                const std::vector<BYTE> aesIV{ concatStr.begin(), concatStr.begin() + 16 };

                // それ以降がデータ

                const std::vector<BYTE> encrypted{ concatStr.begin() + 16, concatStr.end() };

                // 復号化

                std::vector<BYTE> decrypted;

                if (!DecryptAES(aesKey, aesIV, encrypted, &decrypted))
                {
                    traceW(L"fault: DecryptAES");
                    return false;
                }

                // これだと strlen() のサイズと一致しなくなる
                //str.assign(decrypted.begin(), decrypted.end());

                // 入力が '\0' 終端であることを前提に char* から std::string を初期化する

                //str = (char*)decrypted.data();
                //*pInOut = std::move(str);
                *pInOut = std::string((char*)decrypted.data());
            }
        }
    }

    return true;
}

NTSTATUS AwsS3::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    StatsIncr(OnSvcStart);

    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    APP_ASSERT(FileSystem);

    mFileSystem = FileSystem;

    namespace fs = std::filesystem;

    const auto workDir{ fs::path(argWorkDir).wstring() };

    // ファイル・キャッシュ保存用ディレクトリの準備

    const std::wstring cacheDataDir{ workDir + L'\\' + CACHE_DATA_DIR_FNAME };
    if (!MkdirIfNotExists(cacheDataDir))
    {
        traceW(L"%s: can not create directory", cacheDataDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    const std::wstring cacheReportDir{ workDir + L'\\' + CACHE_REPORT_DIR_FNAME };
    if (!MkdirIfNotExists(cacheReportDir))
    {
        traceW(L"%s: can not create directory", cacheReportDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#ifdef _DEBUG
    forEachFiles(cacheDataDir, [this, &LOG_BLOCK()](const auto& wfd, const auto& fullPath)
    {
        APP_ASSERT(!FA_IS_DIR(wfd.dwFileAttributes));

        traceW(L"cache file: [%s]", fullPath.c_str());
    });
#endif

    // ini ファイルから値を取得

    const auto confPath{ workDir + L'\\' + CONFIGFILE_FNAME };
    const auto confPathA{ WC2MB(confPath) };

    //traceW(L"Detect credentials file path is %s", confPath.c_str());

    PCWSTR iniSection = mIniSection.c_str();
    const auto iniSectionA{ WC2MB(mIniSection) };

    // AWS 認証情報

    std::string str_access_key_id;
    std::string str_secret_access_key;
    std::string str_region;

    GetIniStringA(confPathA, iniSectionA.c_str(), "aws_access_key_id", &str_access_key_id);
    GetIniStringA(confPathA, iniSectionA.c_str(), "aws_secret_access_key", &str_secret_access_key);
    GetIniStringA(confPathA, iniSectionA.c_str(), "region", &str_region);

    // レジストリ "HKLM:\SOFTWARE\Microsoft\Cryptography" から "MachineGuid" の値を取得

    std::string secureKeyStr;

    const auto lstatus = GetCryptKeyFromRegistryA(&secureKeyStr);
    if (lstatus != ERROR_SUCCESS)
    {
        traceW(L"fault: GetCryptKeyFromRegistry");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (secureKeyStr.length() < 32)
    {
        traceW(L"%s: illegal data", secureKeyStr.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // MachineGuid の値をキーにして keyid&secret を復号化 (必要なら)

    if (!decryptIfNecessary(secureKeyStr, &str_access_key_id))
    {
        traceA("%s: keyid decrypt fault", str_access_key_id.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    if (!decryptIfNecessary(secureKeyStr, &str_secret_access_key))
    {
        traceA("%s: secret decrypt fault", str_secret_access_key.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    // バケット名フィルタ

    std::wstring bucket_filters_str;

    if (GetIniStringW(confPath, iniSection, L"bucket_filters", &bucket_filters_str))
    {
        std::wistringstream ss{ bucket_filters_str };
        std::wstring token;

        while (std::getline(ss, token, L','))
        {
            const auto pattern{ WildcardToRegexW(TrimW(token)) };

            mBucketFilters.emplace_back(pattern, std::regex_constants::icase);
        }
    }

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

    // S3 クライアントの生成

    mSDKOptions = std::make_unique<Aws::SDKOptions>();
    APP_ASSERT(mSDKOptions);

    Aws::InitAPI(*mSDKOptions);

    Aws::Client::ClientConfiguration config;
    if (str_region.empty())
    {
        // とりあえずデフォルト・リージョンとして設定しておく

        str_region = AWS_DEFAULT_REGION;
    }

    APP_ASSERT(!str_region.empty());
    traceA("region=%s", str_region.c_str());

    // 東京) Aws::Region::AP_NORTHEAST_1;
    // 大阪) Aws::Region::AP_NORTHEAST_3;

    config.region = Aws::String{ str_region.c_str() };

    Aws::S3::S3Client* client = nullptr;

    if (!str_access_key_id.empty() && !str_secret_access_key.empty())
    {
        const Aws::String access_key{ str_access_key_id.c_str() };
        const Aws::String secret_key{ str_secret_access_key.c_str() };

        const Aws::Auth::AWSCredentials credentials{ access_key, secret_key };

        client = new Aws::S3::S3Client(credentials, nullptr, config);

        traceW(L"use credentials");
    }
    else
    {
        client = new Aws::S3::S3Client(config);
    }

    APP_ASSERT(client);
    mClient = ClientPtr(client);

    // S3 接続試験
    traceW(L"Connection test");

    const auto outcome = mClient->ListBuckets();
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: ListBuckets");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // 調整パラメータ

    const auto* confPathCstr = confPath.c_str();

    mConfig.maxDisplayBuckets       = GetIniIntW(confPathCstr, iniSection, L"max_display_buckets",         8,   0, INT_MAX - 1);
    mConfig.maxDisplayObjects       = GetIniIntW(confPathCstr, iniSection, L"max_display_objects",      1000,   0, INT_MAX - 1);
    mConfig.bucketCacheExpiryMin    = GetIniIntW(confPathCstr, iniSection, L"bucket_cache_expiry_min",    20,   1,        1440);
    mConfig.objectCacheExpiryMin    = GetIniIntW(confPathCstr, iniSection, L"object_cache_expiry_min",     3,   1,          60);
    mConfig.cacheFileRetentionMin   = GetIniIntW(confPathCstr, iniSection, L"cache_file_retention_min",   60,   1,       10080);

    mConfig.deleteAfterUpload       = ::GetPrivateProfileIntW(iniSection, L"delete_after_upload",           0, confPathCstr) != 0;
    mConfig.strictFileTimestamp     = ::GetPrivateProfileIntW(iniSection, L"strict_file_timestamp",         0, confPathCstr) != 0;

    // メンバに保存

    mWorkDirCTime = STCTimeToWinFileTimeW(workDir);
    mWorkDir = workDir;
    mCacheDataDir = cacheDataDir;
    mCacheReportDir = cacheReportDir;
    mRegion = MB2WC(str_region);

    //mFileSystem = FileSystem;

    // 長くなったので、外だし

    this->addTasks(START_CALLER0);

    // 外部からの通知待ちイベントの生成

    if (!this->setupNotifListener(START_CALLER0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
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

bool AwsS3::setupNotifListener(CALLER_ARG0)
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

    gNotifWorker = new std::thread(&AwsS3::notifListener, this);
    APP_ASSERT(gNotifWorker);

    const auto hresult = ::SetThreadDescription(gNotifWorker->native_handle(), L"WinCse::notifListener");
    APP_ASSERT(SUCCEEDED(hresult));

    return true;
}

VOID AwsS3::OnSvcStop()
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

                    fwprintf(fp, L"[ListBucketsCache]\n");
                    this->reportListBucketsCache(START_CALLER fp);

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
                clearListBucketsCache(START_CALLER0);
                clearObjectCache(START_CALLER0);
                onIdle(START_CALLER0);
                onTimer(START_CALLER0);

                //reloadListBucketsCache(START_CALLER std::chrono::system_clock::now());

                traceW(L">>>>> CACHE CLEAN <<<<<");

                break;
            }
        }
    }

    traceW(L"thread end");
}

// EOF