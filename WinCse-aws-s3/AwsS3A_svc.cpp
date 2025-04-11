#include "AwsS3A.hpp"
#include <filesystem>
#include <regex>

using namespace WCSE;


static bool decryptIfNecessary(const std::string& secureKeyStr, std::string* pInOut);


NTSTATUS AwsS3A::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    const auto ntstatus = AwsS3B::OnSvcStart(argWorkDir, FileSystem);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault:  AwsS3B::OnSvcStart");
        return ntstatus;
    }

    // ini ファイルから値を取得

    const auto confPathA{ WC2MB(mConfPath) };
    const auto iniSectionA{ WC2MB(mIniSection) };
    const auto iniSection{ iniSectionA.c_str() };

    // バケット名フィルタ

    std::wstring bucket_filters_str;

    if (GetIniStringW(mConfPath, mIniSection.c_str(), L"bucket_filters", &bucket_filters_str))
    {
        std::wistringstream ss{ bucket_filters_str };
        std::wstring token;

        while (std::getline(ss, token, L','))
        {
            const auto pattern{ WildcardToRegexW(TrimW(token)) };

            mBucketFilters.emplace_back(pattern, std::regex_constants::icase);
        }
    }

    // AWS 認証情報

    std::string str_access_key_id;
    std::string str_secret_access_key;
    std::string str_region;

    GetIniStringA(confPathA, iniSection, "aws_access_key_id",     &str_access_key_id);
    GetIniStringA(confPathA, iniSection, "aws_secret_access_key", &str_secret_access_key);
    GetIniStringA(confPathA, iniSection, "region",                &str_region);

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

    // メンバに保存

    mRegion = MB2WC(str_region);

    return STATUS_SUCCESS;
}

VOID AwsS3A::OnSvcStop()
{
    NEW_LOG_BLOCK();

    // デストラクタからも呼ばれるので、再入可能としておくこと

    // AWS S3 処理終了

    if (mSDKOptions)
    {
        traceW(L"aws shutdown");
        Aws::ShutdownAPI(*mSDKOptions);

        mSDKOptions.reset();
    }

    AwsS3B::OnSvcStop();
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

                const std::vector<BYTE> aesKey{ secureKeyStr.cbegin(), secureKeyStr.cend() };

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

// EOF