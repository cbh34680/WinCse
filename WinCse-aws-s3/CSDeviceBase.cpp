#include "CSDeviceBase.hpp"

using namespace WCSE;


static bool decryptIfNecessaryW(const std::wstring& argSecretKey, std::wstring* pInOut);

static PCWSTR CONFIGFILE_FNAME = L"WinCse.conf";
static PCWSTR CACHE_DATA_DIR_FNAME = L"aws-s3\\cache\\data";
static PCWSTR CACHE_REPORT_DIR_FNAME = L"aws-s3\\cache\\report";


CSDeviceBase::CSDeviceBase(const std::wstring&, const std::wstring& argIniSection,
    const std::unordered_map<std::wstring, IWorker*>& argWorkers)
    :
    mIniSection(argIniSection),
    mWorkers(argWorkers)
{
}

NTSTATUS CSDeviceBase::PreCreateFilesystem(FSP_SERVICE*, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argWorkDir);

    // 読み取り専用

    if (VolumeParams->ReadOnlyVolume)
    {
        mDefaultFileAttributes |= FILE_ATTRIBUTE_READONLY;
    }

    //mWinFspService = Service;

    return STATUS_SUCCESS;
}

struct TimerTask : public IScheduledTask
{
    CSDeviceBase* mThat;

    TimerTask(CSDeviceBase* argThat)
        :
        mThat(argThat)
    {
    }

    bool shouldRun(int) const noexcept override
    {
        // 1 分間隔で run() を実行

        return true;
    }

    void run(CALLER_ARG0) override
    {
        mThat->onTimer(CONT_CALLER0);
    }
};

struct IdleTask : public IScheduledTask
{
    CSDeviceBase* mThat;

    IdleTask(CSDeviceBase* argThat)
        :
        mThat(argThat)
    {
    }

    bool shouldRun(int i) const noexcept override
    {
        // 10 分間隔で run() を実行

        return i % 10 == 0;
    }

    void run(CALLER_ARG0) override
    {
        mThat->onIdle(CONT_CALLER0);
    }
};

NTSTATUS CSDeviceBase::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    APP_ASSERT(FileSystem);

    std::wstring workDir{ argWorkDir };
    auto confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

    // 属性参照用ファイル/ディレクトリの準備

    FileHandle refFile = ::CreateFileW
    (
        confPath.c_str(),
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,		// 共有モード
        NULL,														// セキュリティ属性
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL														// テンプレートなし
    );

    if (refFile.invalid())
    {
        traceW(L"fault: CreateFileW, confPath=%s", confPath.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FileHandle refDir = ::CreateFileW
    (
        argWorkDir,
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,     // 共有モード
        NULL,                                                       // セキュリティ属性
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL                                                        // テンプレートなし
    );

    if (refDir.invalid())
    {
        traceW(L"fault: CreateFileW, argWorkDir=%s", argWorkDir);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // ファイル・キャッシュ保存用ディレクトリの準備

    auto cacheDataDir{ workDir + L'\\' + CACHE_DATA_DIR_FNAME };
    if (!MkdirIfNotExists(cacheDataDir))
    {
        traceW(L"%s: can not create directory", cacheDataDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    auto cacheReportDir{ workDir + L'\\' + CACHE_REPORT_DIR_FNAME };
    if (!MkdirIfNotExists(cacheReportDir))
    {
        traceW(L"%s: can not create directory", cacheReportDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#ifdef _DEBUG
    forEachFiles(cacheDataDir, [this, &LOG_BLOCK()](const auto& wfd, const auto& fullPath)
    {
        APP_ASSERT(!FA_IS_DIRECTORY(wfd.dwFileAttributes));

        traceW(L"cache file: [%s]", fullPath.c_str());
    });
#endif

    // ini ファイルから値を取得

    std::wstring clientGuid;
    GetIniStringW(confPath, mIniSection, L"client_guid", &clientGuid);

    if (clientGuid.empty())
    {
        clientGuid = CreateGuidW();
    }

    APP_ASSERT(!clientGuid.empty());

    // ini ファイルから値を取得

    // バケット名フィルタ

    std::vector<std::wregex> bucketFilters;
    std::wstring bucket_filters_str;

    if (GetIniStringW(confPath, mIniSection.c_str(), L"bucket_filters", &bucket_filters_str))
    {
        std::wistringstream ss{ bucket_filters_str };
        std::wstring token;
        std::set<std::wstring> already;

        while (std::getline(ss, token, L' '))
        {
            if (token.empty())
            {
                continue;
            }

            if (already.find(token) != already.cend())
            {
                continue;
            }

            const auto pattern{ WildcardToRegexW(TrimW(token)) };

            try
            {
                bucketFilters.emplace_back(pattern, std::regex_constants::icase);
            }
            catch (const std::regex_error& ex)
            {
                traceA("what=%s", ex.what());
                return STATUS_INVALID_PARAMETER;
            }

            already.insert(token);
        }
    }

    // AWS 接続リージョン

    std::wstring region;
    GetIniStringW(confPath, mIniSection, L"region", &region);

    // 実行時変数

    auto runtimeEnv = std::make_unique<RuntimeEnv>(
        //         ini-path     section         key                             default   min       max
        //----------------------------------------------------------------------------------------------------
        GetIniIntW(confPath,    mIniSection,    L"bucket_cache_expiry_min",         20,   1,        1440),
        bucketFilters,
        cacheDataDir,
        cacheReportDir,
        GetIniIntW(confPath,    mIniSection,    L"cache_file_retention_min",        60,   1,       10080),
        clientGuid,
        STCTimeToWinFileTimeW(workDir),
        mDefaultFileAttributes,
        GetIniBoolW(confPath,   mIniSection,    L"delete_after_upload",         false),
        GetIniIntW(confPath,    mIniSection,    L"delete_dir_condition",             2,   1,           2),
        GetIniIntW(confPath,    mIniSection,    L"max_display_buckets",              8,   0, INT_MAX - 1),
        GetIniIntW(confPath,    mIniSection,    L"max_display_objects",           1000,   0, INT_MAX - 1),
        GetIniIntW(confPath,    mIniSection,    L"object_cache_expiry_min",          5,   1,          60),
        GetIniBoolW(confPath,   mIniSection,    L"readonly",                    false),
        region,
        GetIniBoolW(confPath,   mIniSection,    L"strict_bucket_region",        false),
        GetIniBoolW(confPath,   mIniSection,    L"strict_file_timestamp",       false)
    );

    traceW(L"runtimeEnv=%s", runtimeEnv->str().c_str());

    // AWS 認証情報

    std::wstring accessKeyId;
    std::wstring secretAccessKey;

    GetIniStringW(confPath, mIniSection, L"aws_access_key_id",     &accessKeyId);
    GetIniStringW(confPath, mIniSection, L"aws_secret_access_key", &secretAccessKey);

    // レジストリ "HKLM:\SOFTWARE\Microsoft\Cryptography" から "MachineGuid" の値を取得

    std::wstring regSecretKey;

    const auto lstatus = GetCryptKeyFromRegistryW(&regSecretKey);
    if (lstatus != ERROR_SUCCESS)
    {
        traceW(L"fault: GetCryptKeyFromRegistry");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (regSecretKey.length() < 32)
    {
        traceW(L"%s: illegal data", regSecretKey.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#ifdef _DEBUG
    traceW(L"accessKeyId=%s, secretAccessKey=%s", accessKeyId.c_str(), secretAccessKey.c_str());
#endif

    // MachineGuid の値をキーにして keyid&secret を復号化 (必要なら)

    if (!decryptIfNecessaryW(regSecretKey, &accessKeyId))
    {
        traceW(L"%s: keyid decrypt fault", accessKeyId.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    if (!decryptIfNecessaryW(regSecretKey, &secretAccessKey))
    {
        traceW(L"%s: secret decrypt fault", secretAccessKey.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

#ifdef _DEBUG
    traceW(L"accessKeyId=%s, secretAccessKey=%s", accessKeyId.c_str(), secretAccessKey.c_str());
#endif

    // API 実行オブジェクト

    auto execApi{ std::make_unique<ExecuteApi>(runtimeEnv.get(), region, accessKeyId, secretAccessKey) };
    APP_ASSERT(execApi);

    if (!execApi->Ping(START_CALLER0))
    {
        traceW(L"fault: Ping");
        return STATUS_NETWORK_ACCESS_DENIED;
    }

    // (API 実行オブジェクトを使う) クエリ・オブジェクト

    auto queryBucket{ std::make_unique<QueryBucket>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryBucket);

    auto queryObject{ std::make_unique<QueryObject>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryObject);

    // 外部からの通知待ちスレッドの開始

    if (!this->createNotifListener(START_CALLER0))
    {
        traceW(L"fault: createNotifListener");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // メンバに保存

    //mFileSystem     = FileSystem;
    mRefFile        = std::move(refFile);
    mRefDir         = std::move(refDir);
    mRuntimeEnv     = std::move(runtimeEnv);
    mExecuteApi     = std::move(execApi);
    mQueryBucket    = std::move(queryBucket);
    mQueryObject    = std::move(queryObject);

    // 定期実行タスクを登録

    getWorker(L"timer")->addTask(START_CALLER new TimerTask{ this });

    // アイドル時のタスクを登録

    getWorker(L"timer")->addTask(START_CALLER new IdleTask{ this });

    return STATUS_SUCCESS;
}

VOID CSDeviceBase::OnSvcStop()
{
    // 外部からの通知待ちスレッドの停止

    this->deleteNotifListener(START_CALLER0);
}

static bool decryptIfNecessaryA(const std::string& argSecretKey, std::string* pInOut)
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

                const auto concatB64Str{ str.substr(8) };

                // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する


                // BASE64 文字列をデコード

                std::string concatStr;
                if (!Base64DecodeA(concatB64Str, &concatStr))
                {
                    traceW(L"fault: Base64DecodeA");
                    return false;
                }

                const std::vector<BYTE> concatBytes{ concatStr.cbegin(), concatStr.cend() };

                if (concatBytes.size() < 17)
                {
                    // IV + データなので最低でも 16 + 1 byte は必要

                    traceW(L"fault: concatBytes.size() < 17");
                    return false;
                }

                // 先頭の 16 byte が IV

                const std::vector<BYTE> aesIV{ concatStr.cbegin(), concatStr.cbegin() + 16 };

                // それ以降がデータ

                const std::vector<BYTE> encrypted{ concatStr.cbegin() + 16, concatStr.cend() };

                // 復号化

                std::vector<BYTE> decrypted;

                const std::vector<BYTE> aesKey{ argSecretKey.cbegin(), argSecretKey.cend() };

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

static bool decryptIfNecessaryW(const std::wstring& argSecretKey, std::wstring* pInOut)
{
    const auto secretKey{ WC2MB(argSecretKey) };
    auto data{ WC2MB(*pInOut) };

    if (decryptIfNecessaryA(secretKey, &data))
    {
        *pInOut = MB2WC(data);

        return true;
    }

    return false;
}


// EOF