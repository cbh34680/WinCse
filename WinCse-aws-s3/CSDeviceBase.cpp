#include "CSDeviceBase.hpp"

using namespace CSELIB;
using namespace CSEDAS3;


static bool decryptIfNecessaryW(const std::wstring& argSecretKey, std::wstring* pInOut);

CSDeviceBase::CSDeviceBase(const std::wstring& argIniSection,
    const std::map<std::wstring, IWorker*>& argWorkers)
    :
    mIniSection(argIniSection),
    mWorkers(argWorkers)
{
}

struct TimerTask : public IScheduledTask
{
    CSDeviceBase* mThat;

    TimerTask(CSDeviceBase* argThat)
        :
        mThat(argThat)
    {
    }

    bool shouldRun(int) const override
    {
        // 1 分間隔で run() を実行

        return true;
    }

    void run(int) override
    {
        mThat->onTimer();
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

    bool shouldRun(int argTick) const override
    {
        // 10 分間隔で run() を実行

        return argTick % 10 == 0;
    }

    void run(int) override
    {
        mThat->onIdle();
    }
};

NTSTATUS CSDeviceBase::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM*)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    //APP_ASSERT(FileSystem);

    const auto confPath{ std::filesystem::path{ argWorkDir } / CONFIGFILE_FNAME };

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

    std::list<std::wregex> bucketFilters;
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
                errorA("what=%s", ex.what());
                return STATUS_INVALID_PARAMETER;
            }

            already.insert(token);
        }
    }

    // 無視するファイル名のパターン

    std::optional<std::wregex> ignoreFileNamePatterns;
    std::wstring re_ignore_patterns;

    if (GetIniStringW(confPath, mIniSection, L"re_ignore_patterns", &re_ignore_patterns))
    {
        if (!re_ignore_patterns.empty())
        {
            try
            {
                // conf で指定された正規表現パターンの整合性テスト
                // 不正なパターンの場合は例外で catch されるので反映されない

                auto re{ std::wregex{ re_ignore_patterns, std::regex_constants::icase } };

                // OK

                ignoreFileNamePatterns = std::move(re);
            }
            catch (const std::regex_error& ex)
            {
                errorA("regex_error: %s", ex.what());
                errorW(L"%s: ignored, set default patterns", re_ignore_patterns.c_str());
            }
        }
    }

#ifdef _DEBUG
    if (ignoreFileNamePatterns)
    {
        traceW(L"re_ignore_patterns=[%s]", re_ignore_patterns.c_str());

        const WCHAR* strs[] = {
            LR"(C:\dir\Desktop.ini)",
            LR"(C:\dir\folder.ico)",
            LR"(C:\dir\folder.jpg)",
            LR"(C:\dir\folder.jpeg)",
            LR"(C:\dir\.DS_Store)",
        };

        for (const auto* str: strs)
        {
            traceW(L"str=[%s] %s", str, BOOL_CSTRW(std::regex_search(str, *ignoreFileNamePatterns)));
        }
    }
#endif

    // AWS 接続リージョン

    std::wstring region;
    GetIniStringW(confPath, mIniSection, L"region", &region);

    // 実行時変数

    auto runtimeEnv = std::make_unique<RuntimeEnv>(
        //         ini-path     section         key                             default   min           max
        //----------------------------------------------------------------------------------------------------
        GetIniIntW(confPath,    mIniSection,    L"bucket_cache_expiry_min",         20,     1,        1440),
        bucketFilters,
        clientGuid,
        STCTimeToWinFileTime100nsW(argWorkDir),
        ignoreFileNamePatterns,
        GetIniIntW(confPath,    mIniSection,    L"max_display_buckets",              8,     0, INT_MAX - 1),
        GetIniIntW(confPath,    mIniSection,    L"max_display_objects",           1000,     0, INT_MAX - 1),
        GetIniIntW(confPath,    mIniSection,    L"object_cache_expiry_min",          5,     1,          60),
        region,
        GetIniBoolW(confPath,   mIniSection,    L"strict_bucket_region",        false),
        GetIniBoolW(confPath,   mIniSection,    L"strict_file_timestamp",       false),
        GetIniIntW(confPath,	mIniSection,	L"transfer_write_size_mib",			10,     5,          100)
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
        errorW(L"fault: GetCryptKeyFromRegistry");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (regSecretKey.length() < 32)
    {
        errorW(L"%s: illegal data", regSecretKey.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#ifdef _DEBUG
    traceW(L"accessKeyId=%s, secretAccessKey=%s", accessKeyId.c_str(), secretAccessKey.c_str());
#endif

    // MachineGuid の値をキーにして keyid&secret を復号化 (必要なら)

    if (!decryptIfNecessaryW(regSecretKey, &accessKeyId))
    {
        errorW(L"%s: keyid decrypt fault", accessKeyId.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    if (!decryptIfNecessaryW(regSecretKey, &secretAccessKey))
    {
        errorW(L"%s: secret decrypt fault", secretAccessKey.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    traceW(L"accessKeyId=%s***, secretAccessKey=%s***", accessKeyId.substr(0, 5).c_str(), secretAccessKey.substr(0, 5).c_str());

    // API 実行オブジェクト

    auto execApi{ std::make_unique<ExecuteApi>(getWorker(L"delayed"), runtimeEnv.get(), region, accessKeyId, secretAccessKey) };
    APP_ASSERT(execApi);

    if (!execApi->Ping(START_CALLER0))
    {
        errorW(L"fault: Ping");
        return STATUS_NETWORK_ACCESS_DENIED;
    }

    // (API 実行オブジェクトを使う) クエリ・オブジェクト

    auto queryBucket{ std::make_unique<QueryBucket>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryBucket);

    auto queryObject{ std::make_unique<QueryObject>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryObject);

    // メンバに保存

    //mFileSystem     = FileSystem;
    mRuntimeEnv     = std::move(runtimeEnv);
    mExecuteApi     = std::move(execApi);
    mQueryBucket    = std::move(queryBucket);
    mQueryObject    = std::move(queryObject);

    // 定期実行タスクを登録

    getWorker(L"timer")->addTask(new TimerTask{ this });

    // アイドル時のタスクを登録

    getWorker(L"timer")->addTask(new IdleTask{ this });

    return STATUS_SUCCESS;
}

VOID CSDeviceBase::OnSvcStop()
{
}

static bool decryptIfNecessaryA(const std::string& argSecretKey, std::string* pInOut)
{
    APP_ASSERT(pInOut);

    std::string str{ *pInOut };

    if (!str.empty())
    {
        if (str.length() > 8)
        {
            if (str.substr(0, 8) == "{aes256}")
            {
                NEW_LOG_BLOCK();

                // 先頭の "{aes256}" を除く

                const auto concatB64Str{ str.substr(8) };

                traceA("concatB64Str=%s", concatB64Str.c_str());

                // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する

                // BASE64 文字列をデコード

                std::string concatStr;
                if (!Base64DecodeA(concatB64Str, &concatStr))
                {
                    errorW(L"fault: Base64DecodeA");
                    return false;
                }

                const std::vector<BYTE> concatBytes{ concatStr.cbegin(), concatStr.cend() };

                if (concatBytes.size() < 17)
                {
                    // IV + データなので最低でも 16 + 1 byte は必要

                    errorW(L"fault: concatBytes.size() < 17");
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
                    errorW(L"fault: DecryptAES");
                    return false;
                }

                // これだと strlen() のサイズと一致しなくなる
                //str.assign(decrypted.begin(), decrypted.end());

                // 入力が '\0' 終端であることを前提に char* から std::string を初期化する

                //str = (char*)decrypted.data();
                //*pInOut = std::move(str);

                *pInOut = std::string((char*)decrypted.data());

                traceW(L"success: DecryptAES");
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