#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <filesystem>
#include <inttypes.h>


static const wchar_t* CONFIGFILE_FNAME = L"WinCse.conf";
static const wchar_t* CACHEDIR_FNAME = L"aws-s3\\cache\\data";


static bool forEachFiles(const std::wstring& directory, const std::function<void(const WIN32_FIND_DATA& wfd)>& callback)
{
    WIN32_FIND_DATA wfd = {};
    HANDLE hFind = ::FindFirstFileW((directory + L"\\*").c_str(), &wfd);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    do
    {
        if (wcscmp(wfd.cFileName, L".") != 0 && wcscmp(wfd.cFileName, L"..") != 0)
        {
            callback(wfd);
        }
    }
    while (::FindNextFile(hFind, &wfd) != 0);

    ::FindClose(hFind);

    return true;
}

bool AwsS3::OnSvcStart(const wchar_t* argWorkDir)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argWorkDir);

    bool ret = false;

    try
    {
        namespace fs = std::filesystem;

        std::wstring workDir{ fs::weakly_canonical(fs::path(argWorkDir)).wstring() };

        //
        // ファイル・キャッシュ保存用ディレクトリの準備
        // システムのクリーンアップで自動的に削除されるように、%TMP% に保存する
        //
        std::wstring cacheDir{ mTempDir + L'\\' + CACHEDIR_FNAME };

        if (!mkdirIfNotExists(cacheDir))
        {
            traceW(L"%s: can not create directory", cacheDir.c_str());
            return false;
        }

#ifdef _DEBUG
        forEachFiles(cacheDir, [this, &LOG_BLOCK()](const WIN32_FIND_DATA& wfd)
        {
            traceW(L"cache file: [%s] [%s]",
                wfd.cFileName,
                DecodeLocalNameToFileNameW(wfd.cFileName).c_str());
        });
#endif

        //
        // ini ファイルから値を取得
        //
        const std::wstring confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

        traceW(L"Detect credentials file path is %s", confPath.c_str());

        const std::wstring iniSectionStr{ mIniSection };
        const auto iniSection = iniSectionStr.c_str();

        std::wstring str_access_key_id;
        std::wstring str_secret_access_key;
        std::wstring str_region;

        GetIniStringW(confPath, iniSection, L"aws_access_key_id", &str_access_key_id);
        GetIniStringW(confPath, iniSection, L"aws_secret_access_key", &str_secret_access_key);
        GetIniStringW(confPath, iniSection, L"region", &str_region);

        //
        // バケット名フィルタ
        //
        std::wstring bucket_filters_str;

        if (GetIniStringW(confPath, iniSection, L"bucket_filters", &bucket_filters_str))
        {
            std::wistringstream stream{ bucket_filters_str };
            std::wstring bucket_filter;

            while (std::getline(stream, bucket_filter, L','))
            {
                const auto pattern{ WildcardToRegexW(TrimW(bucket_filter)) };

                mBucketFilters.emplace_back(pattern, std::regex_constants::icase);
            }
        }

        //
        // 最大表示バケット数
        //
        const int maxBuckets = (int)::GetPrivateProfileIntW(iniSection, L"max_buckets", -1, confPath.c_str());

        //
        // 最大表示オブジェクト数
        //
        const int maxObjects = (int)::GetPrivateProfileIntW(iniSection, L"max_objects", 1000, confPath.c_str());

        //
        // S3 クライアントの生成
        //
        mSDKOptions = std::make_shared<Aws::SDKOptions>();
        APP_ASSERT(mSDKOptions);

        Aws::InitAPI(*mSDKOptions);

        Aws::Client::ClientConfiguration config;
        if (str_region.empty())
        {
            // とりあえずデフォルト・リージョンとして設定しておく
            str_region = MB2WC(AWS_DEFAULT_REGION);
        }

        APP_ASSERT(!str_region.empty());

        // 東京) Aws::Region::AP_NORTHEAST_1;
        // 大阪) Aws::Region::AP_NORTHEAST_3;

        config.region = Aws::String{ WC2MB(str_region) };

        Aws::S3::S3Client* client = nullptr;

        if (!str_access_key_id.empty() && !str_secret_access_key.empty())
        {
            const Aws::String access_key{ WC2MB(str_access_key_id) };
            const Aws::String secret_key{ WC2MB(str_secret_access_key) };

            const Aws::Auth::AWSCredentials credentials{ access_key, secret_key };

            client = new Aws::S3::S3Client(credentials, nullptr, config);
        }
        else
        {
            client = new Aws::S3::S3Client(config);
        }

        APP_ASSERT(client);

        //mClient.ptr = std::shared_ptr<Aws::S3::S3Client>(client);
        mClient.ptr = ClientPtr(client);

        //
        // 接続試験
        //
        const auto outcome = mClient.ptr->ListBuckets();
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: test ListBuckets");
            return false;
        }

        mWorkDirTime = STCTimeToWinFileTimeW(workDir);
        mWorkDir = std::move(workDir);
        mCacheDir = std::move(cacheDir);
        mMaxBuckets = maxBuckets;
        mMaxObjects = maxObjects;
        mRegion = std::move(str_region);

        ret = true;
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << "what: " << err.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "unknown error" << std::endl;
    }

    return ret;
}

void AwsS3::OnSvcStop()
{
    NEW_LOG_BLOCK();

    // AWS S3 処理終了

    if (mSDKOptions)
    {
        traceW(L"aws shutdown");
        Aws::ShutdownAPI(*mSDKOptions);
    }
}

struct ListBucketsTask : public ITask
{
    ICloudStorage* storage;

    ListBucketsTask(ICloudStorage* argStorage)
        : storage(argStorage) { }

    std::wstring synonymString()
    {
        return L"ListBucketsTask";
    }

    void run(CALLER_ARG IWorker* worker, const int indent) override
    {
        GetLogger()->traceW_impl(indent, __FUNCTIONW__, __LINE__, __FUNCTIONW__, L"call ListBuckets");

        storage->listBuckets(CONT_CALLER nullptr, {});
    }
};

struct IdleTask : public ITask
{
    AwsS3* s3;

    IdleTask(AwsS3* argThis) : s3(argThis) { }

    void run(CALLER_ARG IWorker* worker, const int indent) override
    {
        GetLogger()->traceW_impl(indent, __FUNCTIONW__, __LINE__, __FUNCTIONW__, L"on Idle");

        s3->OnIdleTime(CONT_CALLER0);
    }
};

void AwsS3::OnIdleTime(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    static int countCalled = 0;
    countCalled++;

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    namespace chrono = std::chrono;
    const auto now { chrono::system_clock::now() };

    //
    // バケット・キャッシュ
    // 
    const auto lastSetTime = mBucketCache.getLastSetTime(CONT_CALLER0);

    if ((now - chrono::minutes(60)) > lastSetTime)
    {
        // バケット・キャッシュを作成してから 60 分以上経過
        traceW(L"need re-load");

        // バケットのキャッシュを削除して、再度一覧を取得する
        mBucketCache.clear(CONT_CALLER0);

        // バケット一覧の取得 --> キャッシュの生成
        listBuckets(CONT_CALLER nullptr, {});
    }
    else
    {
        traceW(L"is valid");
    }

    //
    // オブジェクト・キャッシュ
    //

    // 最終アクセスから 5 分以上経過したオブジェクト・キャッシュを削除

    mObjectCache.deleteOldRecords(CONT_CALLER now - chrono::minutes(5));

    //
    // ファイル・キャッシュ
    //

    // 最終アクセスから 24 時間以上経過したキャッシュ・ファイルを削除する

    APP_ASSERT(std::filesystem::is_directory(mCacheDir));

    const auto nowMillis{ GetCurrentUtcMillis() };

    forEachFiles(mCacheDir, [this, nowMillis, &LOG_BLOCK()](const WIN32_FIND_DATA& wfd)
    {
        const auto lastAccess { WinFileTimeToUtcMillis(wfd.ftLastAccessTime) };

        traceW(L"cache file: [%s] [%s] lastAccess=%" PRIu64,
            wfd.cFileName, DecodeLocalNameToFileNameW(wfd.cFileName).c_str(), lastAccess);

        const auto diffMillis = nowMillis - lastAccess;
        if (diffMillis > (24ULL * 60 * 60 * 1000))
        {
            const auto delPath{ mCacheDir + L'\\' + wfd.cFileName };

            std::error_code ec;
            if (std::filesystem::remove(delPath, ec))
            {
                traceW(L"%s: removed", delPath.c_str());
            }
            else
            {
                traceW(L"%s: remove error", delPath.c_str());
            }
        }
    });

    //
    // 各種情報のログ
    //
    traceW(L"/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/");
    traceW(L"/");
    traceW(L"/         I  N  F  O  R  M  A  T  I  O  N  (%d)", countCalled);
    traceW(L"/");
    traceW(L"/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/");

    traceW(L"ClientPtr.RefCount=%d", mClient.ptr.getRefCount());

    traceW(L"[BucketCache]");
    mBucketCache.report(CONT_CALLER0);

    traceW(L"[ObjectCache]");
    mObjectCache.report(CONT_CALLER0);

#if _DEBUG
    const auto tid = ::GetCurrentThreadId();
    traceW(L"tid=%lu", tid);
#endif
}

bool AwsS3::OnPostSvcStart()
{
    // バケット一覧の先読み
    // 無視できないが優先度は低い
    mDelayedWorker->addTask(new ListBucketsTask{ this }, CanIgnore::NO, Priority::LOW);

    // アイドル時のメモリ解放(等)のタスクを登録
    // 無視できないが優先度は低い
    mIdleWorker->addTask(new IdleTask{ this }, CanIgnore::NO, Priority::LOW);

    return true;
}

void AwsS3::updateVolumeParams(FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(VolumeParams);

    VolumeParams->CaseSensitiveSearch = 1;
    VolumeParams->PersistentAcls = 0;

    VolumeParams->ReadOnlyVolume = 1;

    const UINT32 Timeout = 5000;

    VolumeParams->FileInfoTimeout = Timeout;
    VolumeParams->VolumeInfoTimeout = Timeout;
    VolumeParams->DirInfoTimeout = Timeout;
    VolumeParams->SecurityTimeout = Timeout;
    VolumeParams->StreamInfoTimeout = Timeout;
    VolumeParams->EaTimeout =  Timeout;

    VolumeParams->VolumeInfoTimeoutValid = 1;
    VolumeParams->DirInfoTimeoutValid = 1;
    VolumeParams->SecurityTimeoutValid = 1;
    VolumeParams->StreamInfoTimeoutValid = 1;
    VolumeParams->EaTimeoutValid = 1;

    //wcscpy_s(VolumeParams->Prefix, sizeof VolumeParams->Prefix / sizeof(WCHAR), L"\\\\WinCse.aws-s3");
    wcscpy_s(VolumeParams->FileSystemName, sizeof VolumeParams->FileSystemName / sizeof(WCHAR), L"WinFsp");
}


// EOF