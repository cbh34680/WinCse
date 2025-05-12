#include "CSDeviceBase.hpp"

using namespace CSELIB;
using namespace CSESS3;


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
        this->getClientRegion(),
        GetIniBoolW(confPath,   mIniSection,    L"strict_bucket_region",        false),
        GetIniBoolW(confPath,   mIniSection,    L"strict_file_timestamp",       false),
        GetIniIntW(confPath,	mIniSection,	L"transfer_write_size_mib",			10,     5,          100)
    );

    traceW(L"runtimeEnv=%s", runtimeEnv->str().c_str());

    // API 実行オブジェクト

    auto execApi{ std::make_unique<ExecuteApi>(getWorker(L"delayed"), runtimeEnv.get(), this->getClient()) };
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

void CSDeviceBase::printReport(FILE* fp)
{
    fwprintf(fp, L"[ListBucketsCache]\n");
    mQueryBucket->qbReportCache(START_CALLER fp);

    fwprintf(fp, L"[ObjectCache]\n");
    mQueryObject->qoReportCache(START_CALLER fp);
}

void CSDeviceBase::onTimer()
{
    NEW_LOG_BLOCK();

    // TimerTask から呼び出され、メモリの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    traceW(L"qoDeleteOldCache");

    const auto num = mQueryObject->qoDeleteOldCache(START_CALLER
        now - std::chrono::minutes(mRuntimeEnv->ObjectCacheExpiryMin));

    traceW(L"delete %d records", num);
}

void CSDeviceBase::onIdle()
{
    NEW_LOG_BLOCK();

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    const auto now{ std::chrono::system_clock::now() };

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    // バケット・キャッシュの再作成

    traceW(L"qbReload");

    mQueryBucket->qbReload(START_CALLER
        now - std::chrono::minutes(mRuntimeEnv->BucketCacheExpiryMin));
}

bool CSDeviceBase::onNotif(const std::wstring& argNotifName)
{
    NEW_LOG_BLOCK();

    traceW(L"argNotifName=%s", argNotifName.c_str());

    if (argNotifName == L"Global\\WinCse-util-awss3-clear-cache")
    {
        mQueryBucket->qbClearCache(START_CALLER0);
        mQueryObject->qoClearCache(START_CALLER0);

        this->onTimer();
        this->onIdle();

        traceW(L">>>>> CACHE CLEAN <<<<<");

        return true;
    }

    return false;
}

// EOF