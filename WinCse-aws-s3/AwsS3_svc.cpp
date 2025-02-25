#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <filesystem>
#include <inttypes.h>

using namespace WinCseLib;


static const wchar_t* CONFIGFILE_FNAME = L"WinCse.conf";
static const wchar_t* CACHEDIR_FNAME = L"aws-s3\\cache\\data";      // <-- !!! aws-s3


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

static bool decryptIfNeed(const std::string& secureKeyStr, std::string* pInOut)
{
    APP_ASSERT(pInOut);

    std::string str{ *pInOut };

    if (!str.empty())
    {
        if (str.length() > 8)
        {
            if (str.substr(0, 8) == "{AES256}")
            {
                // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する
                std::vector<BYTE> aesKey{ secureKeyStr.begin(), secureKeyStr.end() };
                std::vector<BYTE> aesIV{ secureKeyStr.begin(), secureKeyStr.begin() + 16 };

                // 先頭の "{AES256}" を除く
                std::string encryptedB64Str{ str.substr(8) };

                // BASE64 文字列をデコード
                std::string encryptedStr = Base64DecodeA(encryptedB64Str);
                std::vector<BYTE> encrypted{ encryptedStr.begin(), encryptedStr.end() };

                // 復号化
                std::vector<BYTE> decrypted;
                if (!DecryptAES(aesKey, aesIV, encrypted, &decrypted))
                {
                    return false;
                }

                // これだと strlen() のサイズと一致しなくなる
                //str.assign(decrypted.begin(), decrypted.end());

                // 入力が '\0' 終端であることを前提に char* から std::string を初期化する
                str = (char*)decrypted.data();

                *pInOut = std::move(str);
            }
        }
    }

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

        const std::wstring workDir{ fs::weakly_canonical(fs::path(argWorkDir)).wstring() };

        //
        // ファイル・キャッシュ保存用ディレクトリの準備
        // システムのクリーンアップで自動的に削除されるように、%TMP% に保存する
        //
        const std::wstring cacheDir{ mTempDir + L'\\' + CACHEDIR_FNAME };

        if (!MkdirIfNotExists(cacheDir))
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
        const std::string confPathA{ WC2MB(confPath) };

        traceW(L"Detect credentials file path is %s", confPath.c_str());

        const wchar_t* iniSection = mIniSection.c_str();
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
        if (!GetCryptKeyFromRegistry(&secureKeyStr))
        {
            traceW(L"fault: GetCryptKeyFromRegistry");
            return false;
        }

        if (secureKeyStr.length() < 32)
        {
            traceW(L"%s: illegal data", secureKeyStr.c_str());
            return false;
        }

        // MachineGuid の値をキーにして keyid&secret を復号化 (必要なら)
        if (!decryptIfNeed(secureKeyStr, &str_access_key_id))
        {
            traceW(L"%s: keyid decrypt fault", str_access_key_id.c_str());
        }

        if (!decryptIfNeed(secureKeyStr, &str_secret_access_key))
        {
            traceW(L"%s: secret decrypt fault", str_secret_access_key.c_str());
        }

        //
        // バケット名フィルタ
        //
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
            str_region = AWS_DEFAULT_REGION;
        }

        APP_ASSERT(!str_region.empty());

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
        mWorkDir = workDir;
        mCacheDir = cacheDir;
        mMaxBuckets = maxBuckets;
        mMaxObjects = maxObjects;
        mRegion = MB2WC(str_region);

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

    // 更新日時から 24 時間以上経過したキャッシュ・ファイルを削除する

    APP_ASSERT(std::filesystem::is_directory(mCacheDir));

    const auto nowMillis{ GetCurrentUtcMillis() };

    forEachFiles(mCacheDir, [this, nowMillis, &LOG_BLOCK()](const WIN32_FIND_DATA& wfd)
    {
        const auto lastAccessTime { WinFileTimeToUtcMillis(wfd.ftLastAccessTime) };

        traceW(L"cache file: [%s] [%s] lastAccess=%" PRIu64,
            wfd.cFileName, DecodeLocalNameToFileNameW(wfd.cFileName).c_str(), lastAccessTime);

        const auto diffMillis = nowMillis - lastAccessTime;
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


// EOF