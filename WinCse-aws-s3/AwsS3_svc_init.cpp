#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


static const wchar_t* CONFIGFILE_FNAME = L"WinCse.conf";
static const wchar_t* CACHE_DATA_DIR_FNAME = L"aws-s3\\cache\\data";
static const wchar_t* CACHE_REPORT_DIR_FNAME = L"aws-s3\\cache\\report";


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
            if (str.substr(0, 8) == "{aes256}")
            {
                // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する
                std::vector<BYTE> aesKey{ secureKeyStr.begin(), secureKeyStr.end() };

                // 先頭の "{aes256}" を除く
                std::string concatB64Str{ str.substr(8) };

                // BASE64 文字列をデコード
                std::string concatStr = Base64DecodeA(concatB64Str);
                std::vector<BYTE> concatBytes{ concatStr.begin(), concatStr.end() };

                if (concatBytes.size() < 17)
                {
                    // IV + データなので最低でも 16 + 1 byte は必要
                    return false;
                }

                // 先頭の 16 byte が IV
                std::vector<BYTE> aesIV{ concatStr.begin(), concatStr.begin() + 16 };

                // それ以降がデータ
                std::vector<BYTE> encrypted{ concatStr.begin() + 16, concatStr.end() };

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

bool AwsS3::PreCreateFilesystem(const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
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
        const std::wstring cacheDataDir{ workDir + L'\\' + CACHE_DATA_DIR_FNAME };
        if (!MkdirIfNotExists(cacheDataDir))
        {
            traceW(L"%s: can not create directory", cacheDataDir.c_str());
            return false;
        }

        const std::wstring cacheReportDir{ workDir + L'\\' + CACHE_REPORT_DIR_FNAME };
        if (!MkdirIfNotExists(cacheReportDir))
        {
            traceW(L"%s: can not create directory", cacheReportDir.c_str());
            return false;
        }

#ifdef _DEBUG
        forEachFiles(cacheDataDir, [this, &LOG_BLOCK()](const WIN32_FIND_DATA& wfd)
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

        // VolumeParams の設定

        VolumeParams->CaseSensitiveSearch = 1;

        const UINT32 Timeout = 2000U;

        VolumeParams->FileInfoTimeout = Timeout;

        //VolumeParams->VolumeInfoTimeout = Timeout;
        VolumeParams->DirInfoTimeout = Timeout;
        //VolumeParams->SecurityTimeout = Timeout;
        //VolumeParams->StreamInfoTimeout = Timeout;
        //VolumeParams->EaTimeout = Timeout;

        //VolumeParams->VolumeInfoTimeoutValid = 1;
        VolumeParams->DirInfoTimeoutValid = 1;
        //VolumeParams->SecurityTimeoutValid = 1;
        //VolumeParams->StreamInfoTimeoutValid = 1;
        //VolumeParams->EaTimeoutValid = 1;

        // 読み取り専用
        const bool readonly = ::GetPrivateProfileIntW(iniSection, L"readonly", 0, confPath.c_str()) != 0;
        if (readonly)
        {
            VolumeParams->ReadOnlyVolume = 1;

            mDefaultFileAttributes |= FILE_ATTRIBUTE_READONLY;
        }

        //
        // S3 クライアントの生成
        //
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
        mCacheDataDir = cacheDataDir;
        mCacheReportDir = cacheReportDir;
        mMaxBuckets = maxBuckets;
        mMaxObjects = maxObjects;
        mRegion = MB2WC(str_region);

        ret = true;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "what: " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "unknown error" << std::endl;
    }

    return ret;
}

void AwsS3::OnIdleTime(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    static int countCalled = 0;
    countCalled++;

    // IdleTask から呼び出され、メモリやファイルの古いものを削除

    namespace chrono = std::chrono;
    const auto now { chrono::system_clock::now() };

    //
    // バケット・キャッシュの再作成
    // 
    this->reloadBukcetsIfNeed(CONT_CALLER0);

    //
    // オブジェクト・キャッシュ
    //

    // 最終アクセスから 5 分以上経過したオブジェクト・キャッシュを削除

    this->deleteOldObjects(CONT_CALLER now - chrono::minutes(5));

    //
    // ファイル・キャッシュ
    //

    // 更新日時から 24 時間以上経過したキャッシュ・ファイルを削除する

    APP_ASSERT(std::filesystem::is_directory(mCacheDataDir));

    const auto nowMillis{ GetCurrentUtcMillis() };

    forEachFiles(mCacheDataDir, [this, nowMillis, &LOG_BLOCK()](const WIN32_FIND_DATA& wfd)
    {
        const auto lastAccessTime { WinFileTimeToUtcMillis(wfd.ftLastAccessTime) };

        traceW(L"cache file: [%s] [%s] lastAccess=%llu",
            wfd.cFileName, DecodeLocalNameToFileNameW(wfd.cFileName).c_str(), lastAccessTime);

        const auto diffMillis = nowMillis - lastAccessTime;
        if (diffMillis > (24ULL * 60 * 60 * 1000))
        {
            const auto delPath{ mCacheDataDir + L'\\' + wfd.cFileName };

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

#if _DEBUG
    const auto tid = ::GetCurrentThreadId();
    traceW(L"tid=%lu", tid);
#endif
}

// EOF