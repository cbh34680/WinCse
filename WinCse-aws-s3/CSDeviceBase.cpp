#include "CSDeviceBase.hpp"

using namespace CSELIB;
using namespace CSEDAS3;


static bool decryptIfNecessaryW(const std::wstring& argSecretKey, std::wstring* pInOut);

static PCWSTR CONFIGFILE_FNAME = L"WinCse.conf";


CSDeviceBase::CSDeviceBase(const std::wstring& argIniSection,
    const std::map<std::wstring, IWorker*>& argWorkers)
    :
    mIniSection(argIniSection),
    mWorkers(argWorkers)
{
}

struct TimerTask : public IScheduledTask
{
    CSDeviceBase* mThat;

    TimerTask(CSDeviceBase* argThat)
        :
        mThat(argThat)
    {
    }

    bool shouldRun(int) const noexcept override
    {
        // 1 ���Ԋu�� run() �����s

        return true;
    }

    void run(int) override
    {
        mThat->onTimer();
    }
};

struct IdleTask : public IScheduledTask
{
    CSDeviceBase* mThat;

    IdleTask(CSDeviceBase* argThat)
        :
        mThat(argThat)
    {
    }

    bool shouldRun(int argTick) const noexcept override
    {
        // 10 ���Ԋu�� run() �����s

        return argTick % 10 == 0;
    }

    void run(int) override
    {
        mThat->onIdle();
    }
};

NTSTATUS CSDeviceBase::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM*)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    //APP_ASSERT(FileSystem);

    const auto confPath{ std::filesystem::path{ argWorkDir } / CONFIGFILE_FNAME };

    // ini �t�@�C������l���擾

    std::wstring clientGuid;
    GetIniStringW(confPath, mIniSection, L"client_guid", &clientGuid);

    if (clientGuid.empty())
    {
        clientGuid = CreateGuidW();
    }

    APP_ASSERT(!clientGuid.empty());

    // ini �t�@�C������l���擾

    // �o�P�b�g���t�B���^

    std::list<std::wregex> bucketFilters;
    std::wstring bucket_filters_str;

    if (GetIniStringW(confPath, mIniSection.c_str(), L"bucket_filters", &bucket_filters_str))
    {
        std::wistringstream ss{ bucket_filters_str };
        std::wstring token;
        std::set<std::wstring> already;

        while (std::getline(ss, token, L' '))
        {
            if (token.empty())
            {
                continue;
            }

            if (already.find(token) != already.cend())
            {
                continue;
            }

            const auto pattern{ WildcardToRegexW(TrimW(token)) };

            try
            {
                bucketFilters.emplace_back(pattern, std::regex_constants::icase);
            }
            catch (const std::regex_error& ex)
            {
                traceA("what=%s", ex.what());
                return STATUS_INVALID_PARAMETER;
            }

            already.insert(token);
        }
    }

    // ��������t�@�C�����̃p�^�[��

    std::optional<std::wregex> ignoreFileNamePatterns;
    std::wstring re_ignore_patterns;

    if (GetIniStringW(confPath, mIniSection, L"re_ignore_patterns", &re_ignore_patterns))
    {
        if (!re_ignore_patterns.empty())
        {
            try
            {
                // conf �Ŏw�肳�ꂽ���K�\���p�^�[���̐������e�X�g
                // �s���ȃp�^�[���̏ꍇ�͗�O�� catch �����̂Ŕ��f����Ȃ�

                auto re{ std::wregex{ re_ignore_patterns, std::regex_constants::icase } };

                // OK

                ignoreFileNamePatterns = std::move(re);
            }
            catch (const std::regex_error& ex)
            {
                traceA("regex_error: %s", ex.what());
                traceW(L"%s: ignored, set default patterns", re_ignore_patterns.c_str());
            }
        }
    }

    // �ǂݎ���p

    const UINT32 defaultFileAttributes = GetIniBoolW(confPath, mIniSection, L"readonly", false) ? FILE_ATTRIBUTE_READONLY : 0;

    // AWS �ڑ����[�W����

    std::wstring region;
    GetIniStringW(confPath, mIniSection, L"region", &region);

    // ���s���ϐ�

    auto runtimeEnv = std::make_unique<RuntimeEnv>(
        //         ini-path     section         key                             default   min       max
        //----------------------------------------------------------------------------------------------------
        GetIniIntW(confPath,    mIniSection,    L"bucket_cache_expiry_min",         20,   1,        1440),
        bucketFilters,
        clientGuid,
        STCTimeToWinFileTime100nsW(argWorkDir),
        defaultFileAttributes,
        ignoreFileNamePatterns,
        GetIniIntW(confPath,    mIniSection,    L"max_display_buckets",              8,   0, INT_MAX - 1),
        GetIniIntW(confPath,    mIniSection,    L"max_display_objects",           1000,   0, INT_MAX - 1),
        GetIniIntW(confPath,    mIniSection,    L"object_cache_expiry_min",          5,   1,          60),
        region,
        GetIniBoolW(confPath,   mIniSection,    L"strict_bucket_region",        false),
        GetIniBoolW(confPath,   mIniSection,    L"strict_file_timestamp",       false)
    );

    traceW(L"runtimeEnv=%s", runtimeEnv->str().c_str());

    // AWS �F�؏��

    std::wstring accessKeyId;
    std::wstring secretAccessKey;

    GetIniStringW(confPath, mIniSection, L"aws_access_key_id",     &accessKeyId);
    GetIniStringW(confPath, mIniSection, L"aws_secret_access_key", &secretAccessKey);

    // ���W�X�g�� "HKLM:\SOFTWARE\Microsoft\Cryptography" ���� "MachineGuid" �̒l���擾

    std::wstring regSecretKey;

    const auto lstatus = GetCryptKeyFromRegistryW(&regSecretKey);
    if (lstatus != ERROR_SUCCESS)
    {
        traceW(L"fault: GetCryptKeyFromRegistry");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (regSecretKey.length() < 32)
    {
        traceW(L"%s: illegal data", regSecretKey.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#ifdef _DEBUG
    traceW(L"accessKeyId=%s, secretAccessKey=%s", accessKeyId.c_str(), secretAccessKey.c_str());
#endif

    // MachineGuid �̒l���L�[�ɂ��� keyid&secret �𕜍��� (�K�v�Ȃ�)

    if (!decryptIfNecessaryW(regSecretKey, &accessKeyId))
    {
        traceW(L"%s: keyid decrypt fault", accessKeyId.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    if (!decryptIfNecessaryW(regSecretKey, &secretAccessKey))
    {
        traceW(L"%s: secret decrypt fault", secretAccessKey.c_str());
        return STATUS_ENCRYPTION_FAILED;
    }

    traceW(L"accessKeyId=%s***, secretAccessKey=%s***", accessKeyId.substr(0, 5).c_str(), secretAccessKey.substr(0, 5).c_str());

    // API ���s�I�u�W�F�N�g

    auto execApi{ std::make_unique<ExecuteApi>(runtimeEnv.get(), region, accessKeyId, secretAccessKey) };
    APP_ASSERT(execApi);

    if (!execApi->Ping(START_CALLER0))
    {
        traceW(L"fault: Ping");
        return STATUS_NETWORK_ACCESS_DENIED;
    }

    // (API ���s�I�u�W�F�N�g���g��) �N�G���E�I�u�W�F�N�g

    auto queryBucket{ std::make_unique<QueryBucket>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryBucket);

    auto queryObject{ std::make_unique<QueryObject>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryObject);

    // �����o�ɕۑ�

    //mFileSystem     = FileSystem;
    mRuntimeEnv     = std::move(runtimeEnv);
    mExecuteApi     = std::move(execApi);
    mQueryBucket    = std::move(queryBucket);
    mQueryObject    = std::move(queryObject);

    // ������s�^�X�N��o�^

    getWorker(L"timer")->addTask(new TimerTask{ this });

    // �A�C�h�����̃^�X�N��o�^

    getWorker(L"timer")->addTask(new IdleTask{ this });

    return STATUS_SUCCESS;
}

VOID CSDeviceBase::OnSvcStop()
{
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

                const std::vector<BYTE> aesKey{ argSecretKey.cbegin(), argSecretKey.cend() };

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