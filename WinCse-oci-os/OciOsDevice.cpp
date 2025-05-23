#include "OciOsDevice.hpp"
#include "aws_sdk_s3_client.h"

using namespace CSELIB;

ICSDevice* NewCSDevice(PCWSTR argIniSection, NamedWorker argWorkers[])
{
    std::map<std::wstring, IWorker*> workers;

    if (NamedWorkersToMap(argWorkers, &workers) <= 0)
    {
        return nullptr;
    }

    for (const auto key: { L"delayed", L"timer", })
    {
        if (workers.find(key) == workers.cend())
        {
            return nullptr;
        }
    }

    return new CSEOOS::OciOsDevice{ argIniSection, workers };
}

namespace CSEOOS {

OciOsDevice::~OciOsDevice()
{
    NEW_LOG_BLOCK();

    // AWS S3 処理終了

    if (mSdkOptions)
    {
        traceW(L"aws shutdown");

        Aws::ShutdownAPI(*mSdkOptions);
        mSdkOptions.reset();
    }
}

NTSTATUS OciOsDevice::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    //APP_ASSERT(FileSystem);

    // AWS SDK の初期化

    APP_ASSERT(!mSdkOptions);
    mSdkOptions = std::make_unique<Aws::SDKOptions>();
    Aws::InitAPI(*mSdkOptions);

    const auto confPath{ std::filesystem::path{ argWorkDir } / CONFIGFILE_FNAME };

    // ini ファイルから値を取得

    // DLL 種類

    std::wstring dllType;
    GetIniStringW(confPath, mIniSection, L"type", &dllType);

    if (dllType != L"oci-os")
    {
        errorW(L"false: DLL type mismatch dllType=%s", dllType.c_str());
        return STATUS_INVALID_PARAMETER;
    }

    // 接続リージョンとネームスペース

    std::wstring regionW;
    std::wstring namespaceW;

    GetIniStringW(confPath, mIniSection, L"region",    &regionW);
    GetIniStringW(confPath, mIniSection, L"namespace", &namespaceW);

    if (regionW.empty())
    {
        errorW(L"fault: regionW empty");
        return STATUS_INVALID_PARAMETER;
    }

    if (namespaceW.empty())
    {
        errorW(L"fault: namespaceW empty");
        return STATUS_INVALID_PARAMETER;
    }

    traceW(L"regionW=%s namespaceW=%s", regionW.c_str(), namespaceW.c_str());

    // 認証情報

    std::wstring accessKeyIdW;
    std::wstring secretAccessKeyW;

    GetIniStringW(confPath, mIniSection, L"aws_access_key_id",     &accessKeyIdW);
    GetIniStringW(confPath, mIniSection, L"aws_secret_access_key", &secretAccessKeyW);

    traceW(L"accessKeyId=%s***, secretAccessKey=%s***", SafeSubStringW(accessKeyIdW, 0, 5).c_str(), SafeSubStringW(secretAccessKeyW, 0, 5).c_str());

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

    if (SafeSubStringW(accessKeyIdW, 0, 8) == L"{aes256}")
    {
        // MachineGuid の値をキーにして keyid&secret を復号化

        auto strInOut{ SafeSubStringW(accessKeyIdW, 8) };

        if (!DecryptCredentialStringW(regSecretKey, &strInOut))
        {
            errorW(L"%s: keyid decrypt fault", accessKeyIdW.c_str());
            return STATUS_ENCRYPTION_FAILED;
        }

        accessKeyIdW = strInOut;
    }

    if (SafeSubStringW(secretAccessKeyW, 0, 8) == L"{aes256}")
    {
        // MachineGuid の値をキーにして keyid&secret を復号化

        auto strInOut{ SafeSubStringW(secretAccessKeyW, 8) };

        if (!DecryptCredentialStringW(regSecretKey, &strInOut))
        {
            errorW(L"%s: secret decrypt fault", secretAccessKeyW.c_str());
            return STATUS_ENCRYPTION_FAILED;
        }

        secretAccessKeyW = strInOut;
    }

    traceW(L"accessKeyId=%s***, secretAccessKey=%s***", SafeSubStringW(accessKeyIdW, 0, 5).c_str(), SafeSubStringW(secretAccessKeyW, 0, 5).c_str());

    // クライアントの生成

    auto regionA{ WC2MB(regionW) };
    auto namespaceA{ WC2MB(namespaceW) };

    Aws::Client::ClientConfiguration config;

    // first param is namespace, 2nd param is region 
    // S3 compatible URL is https://NAMESPACE.compat.objectstorage.REGION.oraclecloud.com/ 

    config.endpointOverride = std::string("https://") + namespaceA + ".compat.objectstorage." + regionA + ".oraclecloud.com/";
    traceA("config.endpointOverride=%s", config.endpointOverride.c_str());

    config.scheme = Aws::Http::Scheme::HTTP;
    config.verifySSL = true;
    config.region = regionA;

    // https://github.com/aws/aws-sdk-cpp/issues/3411#issuecomment-2891706597
    // https://github.com/aws/aws-sdk-cpp/issues/3253#issue-2791519562
    config.checksumConfig.requestChecksumCalculation = Aws::Client::RequestChecksumCalculation::WHEN_REQUIRED;

    Aws::S3::S3Client* s3Client = nullptr;

    if (!accessKeyIdW.empty() && !secretAccessKeyW.empty())
    {
        const auto accessKeyIdA{ WC2MB(accessKeyIdW) };
        const auto secretAccessKeyA{ WC2MB(secretAccessKeyW) };

        const Aws::Auth::AWSCredentials credentials{ accessKeyIdA, secretAccessKeyA };

        s3Client = new Aws::S3::S3Client{ credentials, config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false };

        traceW(L"use credentials");
    }
    else
    {
        s3Client = new Aws::S3::S3Client{ config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false };

        traceW(L"no credentials");
    }

    if (!s3Client)
    {
        errorW(L"fault: new S3Client");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    mClientRegion = regionW;
    mS3Client = std::unique_ptr<Aws::S3::S3Client>{ s3Client };

    // 最後に親クラスの OnSvcStart を呼び出す

    return SdkS3Device::OnSvcStart(argWorkDir, FileSystem);
}

}   // namespace CSEOOS

// EOF