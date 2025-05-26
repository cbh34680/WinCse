#include "AwsS3Device.hpp"

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

    return new CSEAS3::AwsS3Device{ argIniSection, workers };
}

namespace CSEAS3 {

AwsS3Device::~AwsS3Device()
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

NTSTATUS AwsS3Device::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    //APP_ASSERT(FileSystem);

    const auto confPath{ std::filesystem::path{ argWorkDir } / CONFIGFILE_FNAME };

    // ini �t�@�C������l���擾

    // DLL ���

    std::wstring dllType;
    GetIniStringW(confPath, mIniSection, L"type", &dllType);

    if (dllType != L"aws-s3")
    {
        errorW(L"false: DLL type mismatch dllType=%s", dllType.c_str());
        return STATUS_INVALID_PARAMETER;
    }

    // �ڑ����[�W����

    std::wstring regionW;
    GetIniStringW(confPath, mIniSection, L"region", &regionW);

    if (regionW.empty())
    {
        errorW(L"fault: regionW empty");
        return STATUS_INVALID_PARAMETER;
    }

    traceW(L"regionW=%s", regionW.c_str());

    // �F�؏��

    std::wstring accessKeyIdW;
    std::wstring secretAccessKeyW;

    GetIniStringW(confPath, mIniSection, L"aws_access_key_id",     &accessKeyIdW);
    GetIniStringW(confPath, mIniSection, L"aws_secret_access_key", &secretAccessKeyW);

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

    auto regionA{ WC2MB(regionW) };
    config.region = regionA;

    // �N���C�A���g�̐���

    Aws::S3::S3Client* s3Client = nullptr;

    if (!accessKeyIdW.empty() && !secretAccessKeyW.empty())
    {
        const auto accessKeyIdA{ WC2MB(accessKeyIdW) };
        const auto secretAccessKeyA{ WC2MB(secretAccessKeyW) };

        const Aws::Auth::AWSCredentials credentials{ accessKeyIdA, secretAccessKeyA };

        s3Client = new Aws::S3::S3Client{ credentials, nullptr, config };

        traceW(L"use credentials");
    }
    else
    {
        s3Client = new Aws::S3::S3Client{ config };

        traceW(L"no credentials");
    }

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

}   // namespace CSEAS3

// EOF