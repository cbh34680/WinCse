#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


static const wchar_t* CONFIGFILE_FNAME = L"WinCse.conf";
static const wchar_t* CACHE_DATA_DIR_FNAME = L"aws-s3\\cache\\data";
static const wchar_t* CACHE_REPORT_DIR_FNAME = L"aws-s3\\cache\\report";

static bool decryptIfNecessary(const std::string& secureKeyStr, std::string* pInOut)
{
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
                    return false;
                }

                const std::vector<BYTE> concatBytes{ concatStr.begin(), concatStr.end() };

                if (concatBytes.size() < 17)
                {
                    // IV + データなので最低でも 16 + 1 byte は必要

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

bool AwsS3::PreCreateFilesystem(FSP_SERVICE *Service, const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argWorkDir);

    bool ret = false;

    try
    {
        namespace fs = std::filesystem;

        const std::wstring workDir{ fs::weakly_canonical(fs::path(argWorkDir)).wstring() };

        // ファイル・キャッシュ保存用ディレクトリの準備

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
        forEachFiles(cacheDataDir, [this, &LOG_BLOCK()](const auto& wfd, const auto& fullPath)
        {
            APP_ASSERT(!FA_IS_DIR(wfd.dwFileAttributes));

            traceW(L"cache file: [%s]", fullPath.c_str());
        });
#endif

        // ini ファイルから値を取得

        const std::wstring confPath{ workDir + L'\\' + CONFIGFILE_FNAME };
        const std::string confPathA{ WC2MB(confPath) };

        //traceW(L"Detect credentials file path is %s", confPath.c_str());

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
        if (!GetCryptKeyFromRegistryA(&secureKeyStr))
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

        if (!decryptIfNecessary(secureKeyStr, &str_access_key_id))
        {
            traceW(L"%s: keyid decrypt fault", str_access_key_id.c_str());
        }

        if (!decryptIfNecessary(secureKeyStr, &str_secret_access_key))
        {
            traceW(L"%s: secret decrypt fault", str_secret_access_key.c_str());
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

        // 読み取り専用

        if (VolumeParams->ReadOnlyVolume)
        {
            mDefaultFileAttributes |= FILE_ATTRIBUTE_READONLY;
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
            traceW(L"file open error: %s", confPath.c_str());
            return false;
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
            traceW(L"file open error: %s", argWorkDir);
            return false;
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
        mClient = ClientPtr(client);

        // S3 接続試験

        const auto outcome = mClient->ListBuckets();
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: test ListBuckets");
            return false;
        }

        // 調整パラメータ

        const auto* confPathCstr = confPath.c_str();

        mConfig.maxDisplayBuckets       = GetIniIntW(confPathCstr, iniSection, L"max_display_buckets",         8,   0, INT_MAX - 1);
        mConfig.maxDisplayObjects       = GetIniIntW(confPathCstr, iniSection, L"max_display_objects",      1000,   0, INT_MAX - 1);
        mConfig.bucketCacheExpiryMin    = GetIniIntW(confPathCstr, iniSection, L"bucket_cache_expiry_min",    20,   1,        1440);
        mConfig.objectCacheExpiryMin    = GetIniIntW(confPathCstr, iniSection, L"object_cache_expiry_min",     3,   1,          60);
        mConfig.cacheFileRetentionMin   = GetIniIntW(confPathCstr, iniSection, L"cache_file_retention_min",  360,   1,       10080);

        mConfig.deleteAfterUpload       = ::GetPrivateProfileIntW(iniSection, L"delete_after_upload",           0, confPathCstr) != 0;
        mConfig.strictFileTimestamp     = ::GetPrivateProfileIntW(iniSection, L"strict_file_timestamp",         0, confPathCstr) != 0;

        // メンバに保存して終了

        mWinFspService = Service;
        mWorkDirCTime = STCTimeToWinFileTimeW(workDir);
        mWorkDir = workDir;
        mCacheDataDir = cacheDataDir;
        mCacheReportDir = cacheReportDir;
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

// EOF