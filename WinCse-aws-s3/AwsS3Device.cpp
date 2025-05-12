#include "AwsS3Device.hpp"

static bool decryptIfNecessaryW(const std::wstring& argSecretKey, std::wstring* pInOut);

using namespace CSELIB;
using namespace CSEAS3;


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

    return new AwsS3Device(argIniSection, workers);
}

AwsS3Device::AwsS3Device(const std::wstring& argIniSection, const std::map<std::wstring, CSELIB::IWorker*>& argWorkers)
    :
    CSDevice(argIniSection, argWorkers)
{
    mSdkOptions = std::make_unique<Aws::SDKOptions>();
    Aws::InitAPI(*mSdkOptions);
}

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

    // AWS �ڑ����[�W����

    std::wstring regionW;
    GetIniStringW(confPath, mIniSection, L"region", &regionW);

    // AWS �F�؏��

    std::wstring accessKeyIdW;
    std::wstring secretAccessKeyW;

    GetIniStringW(confPath, mIniSection, L"aws_access_key_id",     &accessKeyIdW);
    GetIniStringW(confPath, mIniSection, L"aws_secret_access_key", &secretAccessKeyW);

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

#ifdef _DEBUG
    traceW(L"accessKeyIdW=%s, secretAccessKeyW=%s", accessKeyIdW.c_str(), secretAccessKeyW.c_str());
#endif

    // MachineGuid �̒l���L�[�ɂ��� keyid&secret �𕜍��� (�K�v�Ȃ�)

    if (!decryptIfNecessaryW(regSecretKey, &accessKeyIdW))
    {
        errorW(L"%s: keyid decrypt fault", accessKeyIdW.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    if (!decryptIfNecessaryW(regSecretKey, &secretAccessKeyW))
    {
        errorW(L"%s: secret decrypt fault", secretAccessKeyW.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    traceW(L"accessKeyId=%s***, secretAccessKey=%s***", accessKeyIdW.substr(0, 5).c_str(), secretAccessKeyW.substr(0, 5).c_str());

    // S3 �N���C�A���g�̐���

    auto regionA{ WC2MB(regionW) };

    Aws::Client::ClientConfiguration config;
    if (regionA.empty())
    {
        // �Ƃ肠�����f�t�H���g�E���[�W�����Ƃ��Đݒ肵�Ă���

        traceA("argRegion empty, set default");

        regionA = AWS_DEFAULT_REGION;
    }

    traceA("regionA=%s", regionA.c_str());

    // ����) Aws::Region::AP_NORTHEAST_1;
    // ���) Aws::Region::AP_NORTHEAST_3;

    config.region = regionA;

    Aws::S3::S3Client* s3Client = nullptr;

    if (!accessKeyIdW.empty() && !secretAccessKeyW.empty())
    {
        const auto accessKeyIdA{ WC2MB(accessKeyIdW) };
        const auto secretAccessKeyA{ WC2MB(secretAccessKeyW) };

        const Aws::Auth::AWSCredentials credentials{ accessKeyIdA, secretAccessKeyA };

        s3Client = new Aws::S3::S3Client(credentials, nullptr, config);

        traceW(L"use credentials");
    }
    else
    {
        s3Client = new Aws::S3::S3Client(config);

        traceW(L"no credentials");
    }

    if (!s3Client)
    {
        errorW(L"fault: new S3Client");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    mRegion = regionW;
    mS3Client = std::unique_ptr<Aws::S3::S3Client>(s3Client);


    // �Ō�ɐe�N���X�� OnSvcStart ���Ăяo��

    return CSDevice::OnSvcStart(argWorkDir, FileSystem);
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

                // �擪�� "{aes256}" ������

                const auto concatB64Str{ str.substr(8) };

                traceA("concatB64Str=%s", concatB64Str.c_str());

                // MachineGuid �̒l�� AES �� key �Ƃ��Aiv �ɂ� key[0..16] ��ݒ肷��

                // BASE64 ��������f�R�[�h

                std::string concatStr;
                if (!Base64DecodeA(concatB64Str, &concatStr))
                {
                    errorW(L"fault: Base64DecodeA");
                    return false;
                }

                const std::vector<BYTE> concatBytes{ concatStr.cbegin(), concatStr.cend() };

                if (concatBytes.size() < 17)
                {
                    // IV + �f�[�^�Ȃ̂ōŒ�ł� 16 + 1 byte �͕K�v

                    errorW(L"fault: concatBytes.size() < 17");
                    return false;
                }

                // �擪�� 16 byte �� IV

                const std::vector<BYTE> aesIV{ concatStr.cbegin(), concatStr.cbegin() + 16 };

                // ����ȍ~���f�[�^

                const std::vector<BYTE> encrypted{ concatStr.cbegin() + 16, concatStr.cend() };

                // ������

                std::vector<BYTE> decrypted;

                const std::vector<BYTE> aesKey{ argSecretKey.cbegin(), argSecretKey.cend() };

                if (!DecryptAES(aesKey, aesIV, encrypted, &decrypted))
                {
                    errorW(L"fault: DecryptAES");
                    return false;
                }

                // ���ꂾ�� strlen() �̃T�C�Y�ƈ�v���Ȃ��Ȃ�
                //str.assign(decrypted.begin(), decrypted.end());

                // ���͂� '\0' �I�[�ł��邱�Ƃ�O��� char* ���� std::string ������������

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