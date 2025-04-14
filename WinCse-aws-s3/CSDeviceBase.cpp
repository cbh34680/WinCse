#include "CSDeviceBase.hpp"


using namespace WCSE;

static bool decryptIfNecessaryW(const std::wstring& argSecretKey, std::wstring* pInOut);

static PCWSTR CONFIGFILE_FNAME = L"WinCse.conf";
static PCWSTR CACHE_DATA_DIR_FNAME = L"aws-s3\\cache\\data";
static PCWSTR CACHE_REPORT_DIR_FNAME = L"aws-s3\\cache\\report";


CSDeviceBase::CSDeviceBase(const std::wstring&, const std::wstring& argIniSection,
    const std::unordered_map<std::wstring, IWorker*>& argWorkers)
    :
    mIniSection(argIniSection),
    mWorkers(argWorkers)
{
}

NTSTATUS CSDeviceBase::PreCreateFilesystem(FSP_SERVICE*, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argWorkDir);

    // �ǂݎ���p

    if (VolumeParams->ReadOnlyVolume)
    {
        mDefaultFileAttributes |= FILE_ATTRIBUTE_READONLY;
    }

    //mWinFspService = Service;

    return STATUS_SUCCESS;
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

    void run(CALLER_ARG0) override
    {
        mThat->onTimer(CONT_CALLER0);
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

    bool shouldRun(int i) const noexcept override
    {
        // 10 ���Ԋu�� run() �����s

        return i % 10 == 0;
    }

    void run(CALLER_ARG0) override
    {
        mThat->onIdle(CONT_CALLER0);
    }
};

NTSTATUS CSDeviceBase::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    APP_ASSERT(FileSystem);

    std::wstring workDir{ argWorkDir };
    auto confPath{ workDir + L'\\' + CONFIGFILE_FNAME };

    // �����Q�Ɨp�t�@�C��/�f�B���N�g���̏���

    FileHandle refFile = ::CreateFileW
    (
        confPath.c_str(),
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,		// ���L���[�h
        NULL,														// �Z�L�����e�B����
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL														// �e���v���[�g�Ȃ�
    );

    if (refFile.invalid())
    {
        traceW(L"fault: CreateFileW, confPath=%s", confPath.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FileHandle refDir = ::CreateFileW
    (
        argWorkDir,
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,     // ���L���[�h
        NULL,                                                       // �Z�L�����e�B����
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL                                                        // �e���v���[�g�Ȃ�
    );

    if (refDir.invalid())
    {
        traceW(L"fault: CreateFileW, argWorkDir=%s", argWorkDir);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // �t�@�C���E�L���b�V���ۑ��p�f�B���N�g���̏���

    auto cacheDataDir{ workDir + L'\\' + CACHE_DATA_DIR_FNAME };
    if (!MkdirIfNotExists(cacheDataDir))
    {
        traceW(L"%s: can not create directory", cacheDataDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    auto cacheReportDir{ workDir + L'\\' + CACHE_REPORT_DIR_FNAME };
    if (!MkdirIfNotExists(cacheReportDir))
    {
        traceW(L"%s: can not create directory", cacheReportDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#ifdef _DEBUG
    forEachFiles(cacheDataDir, [this, &LOG_BLOCK()](const auto& wfd, const auto& fullPath)
    {
        APP_ASSERT(!FA_IS_DIRECTORY(wfd.dwFileAttributes));

        traceW(L"cache file: [%s]", fullPath.c_str());
    });
#endif

    // ini �t�@�C������l���擾

    const auto iniSection = mIniSection.c_str();
    const auto confPathCstr = confPath.c_str();

    std::wstring clientGuid;
    GetIniStringW(confPathCstr, iniSection, L"client_guid", &clientGuid);

    if (clientGuid.empty())
    {
        clientGuid = CreateGuidW();
    }

    APP_ASSERT(!clientGuid.empty());

    // ini �t�@�C������l���擾

    // �o�P�b�g���t�B���^

    std::vector<std::wregex> bucketFilters;
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
            bucketFilters.emplace_back(pattern, std::regex_constants::icase);
            already.insert(token);
        }
    }

    // AWS �ڑ����[�W����

    std::wstring region;
    GetIniStringW(confPath, iniSection, L"region", &region);

    // ���s���ϐ�

    auto runtimeEnv = std::make_unique<RuntimeEnv>(
        GetIniIntW(confPathCstr, iniSection, L"bucket_cache_expiry_min",    20,   1,        1440),
        bucketFilters,
        cacheDataDir,
        cacheReportDir,
        GetIniIntW(confPathCstr, iniSection, L"cache_file_retention_min",   60,   1,       10080),
        clientGuid,
        STCTimeToWinFileTimeW(workDir),
        mDefaultFileAttributes,
        ::GetPrivateProfileIntW(iniSection,  L"delete_after_upload",         0,     confPathCstr),
        GetIniIntW(confPathCstr, iniSection, L"max_display_buckets",         8,   0, INT_MAX - 1),
        GetIniIntW(confPathCstr, iniSection, L"max_display_objects",      1000,   0, INT_MAX - 1),
        GetIniIntW(confPathCstr, iniSection, L"object_cache_expiry_min",     5,   1,          60),
        region,
        ::GetPrivateProfileIntW(iniSection,  L"strict_file_timestamp",       0,     confPathCstr)
    );

    traceW(L"runtimeEnv=%s", runtimeEnv->str().c_str());

    // AWS �F�؏��

    std::wstring accessKeyId;
    std::wstring secretAccessKey;

    GetIniStringW(confPath, iniSection, L"aws_access_key_id",     &accessKeyId);
    GetIniStringW(confPath, iniSection, L"aws_secret_access_key", &secretAccessKey);

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

#ifdef _DEBUG
    traceW(L"accessKeyId=%s, secretAccessKey=%s", accessKeyId.c_str(), secretAccessKey.c_str());
#endif

    auto execApi{ std::make_unique<ExecuteApi>(runtimeEnv.get(), region, accessKeyId, secretAccessKey) };
    APP_ASSERT(execApi);

    if (!execApi->Ping(START_CALLER0))
    {
        traceW(L"fault: Ping");
        return STATUS_ENCRYPTION_FAILED;
    }

    auto queryBucket{ std::make_unique<QueryBucket>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryBucket);

    auto queryObject{ std::make_unique<QueryObject>(runtimeEnv.get(), execApi.get()) };
    APP_ASSERT(queryObject);

    // �O������̒ʒm�҂��X���b�h�̊J�n

    if (!this->createNotifListener(START_CALLER0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // �����o�ɕۑ�

    //mFileSystem     = FileSystem;
    mRefFile        = std::move(refFile);
    mRefDir         = std::move(refDir);
    mRuntimeEnv     = std::move(runtimeEnv);
    mExecuteApi     = std::move(execApi);
    mQueryBucket    = std::move(queryBucket);
    mQueryObject    = std::move(queryObject);

    // ������s�^�X�N��o�^

    getWorker(L"timer")->addTask(START_CALLER new TimerTask{ this });

    // �A�C�h�����̃^�X�N��o�^

    getWorker(L"timer")->addTask(START_CALLER new IdleTask{ this });

    return STATUS_SUCCESS;
}

VOID CSDeviceBase::OnSvcStop()
{
    // �O������̒ʒm�҂��X���b�h�̒�~

    this->deleteNotifListener(START_CALLER0);
}

static bool decryptIfNecessaryA(const std::string& argSecretKey, std::string* pInOut)
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