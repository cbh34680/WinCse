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

    // ini �t�@�C������l���擾

    const auto confPathA{ WC2MB(mConfPath) };
    const auto iniSectionA{ WC2MB(mIniSection) };
    const auto iniSection{ iniSectionA.c_str() };

    // �o�P�b�g���t�B���^

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

    // AWS �F�؏��

    std::string str_access_key_id;
    std::string str_secret_access_key;
    std::string str_region;

    GetIniStringA(confPathA, iniSection, "aws_access_key_id",     &str_access_key_id);
    GetIniStringA(confPathA, iniSection, "aws_secret_access_key", &str_secret_access_key);
    GetIniStringA(confPathA, iniSection, "region",                &str_region);

    // ���W�X�g�� "HKLM:\SOFTWARE\Microsoft\Cryptography" ���� "MachineGuid" �̒l���擾

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

    // MachineGuid �̒l���L�[�ɂ��� keyid&secret �𕜍��� (�K�v�Ȃ�)

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

    // S3 �N���C�A���g�̐���

    mSDKOptions = std::make_unique<Aws::SDKOptions>();
    APP_ASSERT(mSDKOptions);

    Aws::InitAPI(*mSDKOptions);

    Aws::Client::ClientConfiguration config;
    if (str_region.empty())
    {
        // �Ƃ肠�����f�t�H���g�E���[�W�����Ƃ��Đݒ肵�Ă���

        str_region = AWS_DEFAULT_REGION;
    }

    APP_ASSERT(!str_region.empty());
    traceA("region=%s", str_region.c_str());

    // ����) Aws::Region::AP_NORTHEAST_1;
    // ���) Aws::Region::AP_NORTHEAST_3;

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

    // S3 �ڑ�����
    traceW(L"Connection test");

    const auto outcome = mClient->ListBuckets();
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: ListBuckets");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // �����o�ɕۑ�

    mRegion = MB2WC(str_region);

    return STATUS_SUCCESS;
}

VOID AwsS3A::OnSvcStop()
{
    NEW_LOG_BLOCK();

    // �f�X�g���N�^������Ă΂��̂ŁA�ē��\�Ƃ��Ă�������

    // AWS S3 �����I��

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
                // �擪�� "{aes256}" ������

                const auto concatB64Str{ str.substr(8) };

                // MachineGuid �̒l�� AES �� key �Ƃ��Aiv �ɂ� key[0..16] ��ݒ肷��


                // BASE64 ��������f�R�[�h

                std::string concatStr;
                if (!Base64DecodeA(concatB64Str, &concatStr))
                {
                    traceW(L"fault: Base64DecodeA");
                    return false;
                }

                const std::vector<BYTE> concatBytes{ concatStr.cbegin(), concatStr.cend() };

                if (concatBytes.size() < 17)
                {
                    // IV + �f�[�^�Ȃ̂ōŒ�ł� 16 + 1 byte �͕K�v

                    traceW(L"fault: concatBytes.size() < 17");
                    return false;
                }

                // �擪�� 16 byte �� IV

                const std::vector<BYTE> aesIV{ concatStr.cbegin(), concatStr.cbegin() + 16 };

                // ����ȍ~���f�[�^

                const std::vector<BYTE> encrypted{ concatStr.cbegin() + 16, concatStr.cend() };

                // ������

                std::vector<BYTE> decrypted;

                const std::vector<BYTE> aesKey{ secureKeyStr.cbegin(), secureKeyStr.cend() };

                if (!DecryptAES(aesKey, aesIV, encrypted, &decrypted))
                {
                    traceW(L"fault: DecryptAES");
                    return false;
                }

                // ���ꂾ�� strlen() �̃T�C�Y�ƈ�v���Ȃ��Ȃ�
                //str.assign(decrypted.begin(), decrypted.end());

                // ���͂� '\0' �I�[�ł��邱�Ƃ�O��� char* ���� std::string ������������

                //str = (char*)decrypted.data();
                //*pInOut = std::move(str);
                *pInOut = std::string((char*)decrypted.data());
            }
        }
    }

    return true;
}

// EOF