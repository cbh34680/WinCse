#include "CompatS3Device.hpp"

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

    return new CSECS3::CompatS3Device{ argIniSection, workers };
}

namespace CSECS3 {

CompatS3Device::~CompatS3Device()
{
    NEW_LOG_BLOCK();

    // AWS S3 �����I��

    if (mSdkOptions)
    {
        traceW(L"aws shutdown");

        Aws::ShutdownAPI(*mSdkOptions);
        mSdkOptions.reset();
    }
}

NTSTATUS CompatS3Device::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    //APP_ASSERT(FileSystem);

    const auto confPath{ std::filesystem::path{ argWorkDir } / CONFIGFILE_FNAME };

    // ini �t�@�C������l���擾

    // DLL ���

    std::wstring dllType;
    GetIniStringW(confPath, mIniSection, L"type", &dllType);

    if (dllType != L"compat-s3")
    {
        errorW(L"false: DLL type mismatch dllType=%s", dllType.c_str());
        return STATUS_INVALID_PARAMETER;
    }

    // �ڑ����[�W�����ƃl�[���X�y�[�X

    std::wstring regionW;
    std::wstring endpointW;

    GetIniStringW(confPath, mIniSection, L"region",   &regionW);
    GetIniStringW(confPath, mIniSection, L"endpoint", &endpointW);

    if (regionW.empty())
    {
        errorW(L"fault: regionW empty");
        return STATUS_INVALID_PARAMETER;
    }

    if (endpointW.empty())
    {
        errorW(L"fault: endpointW empty");
        return STATUS_INVALID_PARAMETER;
    }

    traceW(L"regionW=%s endpointW=%s", regionW.c_str(), endpointW.c_str());

    // �F�؏��

    std::wstring accessKeyIdW;
    std::wstring secretAccessKeyW;

    GetIniStringW(confPath, mIniSection, L"aws_access_key_id",     &accessKeyIdW);
    GetIniStringW(confPath, mIniSection, L"aws_secret_access_key", &secretAccessKeyW);

    if (accessKeyIdW.empty())
    {
        errorW(L"fault: accessKeyIdW empty");
        return STATUS_INVALID_PARAMETER;
    }

    if (secretAccessKeyW.empty())
    {
        errorW(L"fault: secretAccessKeyW empty");
        return STATUS_INVALID_PARAMETER;
    }

    traceW(L"accessKeyId=%s***, secretAccessKey=%s***", SafeSubStringW(accessKeyIdW, 0, 5).c_str(), SafeSubStringW(secretAccessKeyW, 0, 5).c_str());

    // ���W�X�g�� "HKLM:\SOFTWARE\Microsoft\Cryptography" ���� "MachineGuid" �̒l���擾

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
        // MachineGuid �̒l���L�[�ɂ��� keyid&secret �𕜍���

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
        // MachineGuid �̒l���L�[�ɂ��� keyid&secret �𕜍���

        auto strInOut{ SafeSubStringW(secretAccessKeyW, 8) };

        if (!DecryptCredentialStringW(regSecretKey, &strInOut))
        {
            errorW(L"%s: secret decrypt fault", secretAccessKeyW.c_str());
            return STATUS_ENCRYPTION_FAILED;
        }

        secretAccessKeyW = strInOut;
    }

    traceW(L"accessKeyId=%s***, secretAccessKey=%s***", SafeSubStringW(accessKeyIdW, 0, 5).c_str(), SafeSubStringW(secretAccessKeyW, 0, 5).c_str());

    // AWS SDK �̏�����

    APP_ASSERT(!mSdkOptions);
    mSdkOptions = std::make_unique<Aws::SDKOptions>();
    Aws::InitAPI(*mSdkOptions);

    Aws::Client::ClientConfiguration config;

    // ���N�G�X�g CheckSum

    std::wstring requestChecksumCalculation;

    if (GetIniStringW(confPath, mIniSection, L"s3.request_checksum_calculation", &requestChecksumCalculation))
    {
        if (requestChecksumCalculation == L"WHEN_SUPPORTED")
        {
            config.checksumConfig.requestChecksumCalculation = Aws::Client::RequestChecksumCalculation::WHEN_SUPPORTED;
        }
        else if (requestChecksumCalculation == L"WHEN_REQUIRED")
        {
            config.checksumConfig.requestChecksumCalculation = Aws::Client::RequestChecksumCalculation::WHEN_REQUIRED;
        }
        else
        {
            errorW(L"invalid: s3.request_checksum_calculation=%s (ignore)", requestChecksumCalculation.c_str());
        }
    }

    // ���X�|���X CheckSum

    std::wstring responseChecksumValidation;

    if (GetIniStringW(confPath, mIniSection, L"s3.response_checksum_validation", &responseChecksumValidation))
    {
        if (responseChecksumValidation == L"WHEN_SUPPORTED")
        {
            config.checksumConfig.responseChecksumValidation = Aws::Client::ResponseChecksumValidation::WHEN_SUPPORTED;
        }
        else if (responseChecksumValidation == L"WHEN_REQUIRED")
        {
            config.checksumConfig.responseChecksumValidation = Aws::Client::ResponseChecksumValidation::WHEN_REQUIRED;
        }
        else
        {
            errorW(L"invalid: s3.response_checksum_validation=%s (ignore)", responseChecksumValidation.c_str());
        }
    }

    // �N���C�A���g�̐���

    auto regionA{ WC2MB(regionW) };
    auto endpointA{ WC2MB(endpointW) };

    config.endpointOverride = endpointA;
    traceA("config.endpointOverride=%s", config.endpointOverride.c_str());

    config.scheme = Aws::Http::Scheme::HTTP;
    config.verifySSL = true;
    config.region = regionA;

    const auto accessKeyIdA{ WC2MB(accessKeyIdW) };
    const auto secretAccessKeyA{ WC2MB(secretAccessKeyW) };

    const Aws::Auth::AWSCredentials credentials{ accessKeyIdA, secretAccessKeyA };

    Aws::S3::S3Client* s3Client = new Aws::S3::S3Client{ credentials, config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false };

    traceW(L"use credentials");

    if (!s3Client)
    {
        errorW(L"fault: new S3Client");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    mClientRegion = regionW;
    mS3Client = std::unique_ptr<Aws::S3::S3Client>{ s3Client };

    // �Ō�ɐe�N���X�� OnSvcStart ���Ăяo��

    return CSDevice::OnSvcStart(argWorkDir, FileSystem);
}

}   // namespace CSEOOS

// EOF