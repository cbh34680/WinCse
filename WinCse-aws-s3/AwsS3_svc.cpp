#include "WinCseLib.h"
#include "AwsS3.hpp"
#include <filesystem>
#include <inttypes.h>

using namespace WinCseLib;


static const wchar_t* CONFIGFILE_FNAME = L"WinCse.conf";
static const wchar_t* CACHEDIR_FNAME = L"aws-s3\\cache\\data";      // <-- !!! aws-s3


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
            if (str.substr(0, 8) == "{AES256}")
            {
                // MachineGuid �̒l�� AES �� key �Ƃ��Aiv �ɂ� key[0..16] ��ݒ肷��
                std::vector<BYTE> aesKey{ secureKeyStr.begin(), secureKeyStr.end() };
                std::vector<BYTE> aesIV{ secureKeyStr.begin(), secureKeyStr.begin() + 16 };

                // �擪�� "{AES256}" ������
                std::string encryptedB64Str{ str.substr(8) };

                // BASE64 ��������f�R�[�h
                std::string encryptedStr = Base64DecodeA(encryptedB64Str);
                std::vector<BYTE> encrypted{ encryptedStr.begin(), encryptedStr.end() };

                // ������
                std::vector<BYTE> decrypted;
                if (!DecryptAES(aesKey, aesIV, encrypted, &decrypted))
                {
                    return false;
                }

                // ���ꂾ�� strlen() �̃T�C�Y�ƈ�v���Ȃ��Ȃ�
                //str.assign(decrypted.begin(), decrypted.end());

                // ���͂� '\0' �I�[�ł��邱�Ƃ�O��� char* ���� std::string ������������
                str = (char*)decrypted.data();

                *pInOut = std::move(str);
            }
        }
    }

    return true;
}

bool AwsS3::OnSvcStart(const wchar_t* argWorkDir)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argWorkDir);

    bool ret = false;

    try
    {
        namespace fs = std::filesystem;

        const std::wstring workDir{ fs::weakly_canonical(fs::path(argWorkDir)).wstring() };

        //
        // �t�@�C���E�L���b�V���ۑ��p�f�B���N�g���̏���
        // �V�X�e���̃N���[���A�b�v�Ŏ����I�ɍ폜�����悤�ɁA%TMP% �ɕۑ�����
        //
        const std::wstring cacheDir{ mTempDir + L'\\' + CACHEDIR_FNAME };

        if (!MkdirIfNotExists(cacheDir))
        {
            traceW(L"%s: can not create directory", cacheDir.c_str());
            return false;
        }

#ifdef _DEBUG
        forEachFiles(cacheDir, [this, &LOG_BLOCK()](const WIN32_FIND_DATA& wfd)
        {
            traceW(L"cache file: [%s] [%s]",
                wfd.cFileName,
                DecodeLocalNameToFileNameW(wfd.cFileName).c_str());
        });
#endif

        //
        // ini �t�@�C������l���擾
        //
        const std::wstring confPath{ workDir + L'\\' + CONFIGFILE_FNAME };
        const std::string confPathA{ WC2MB(confPath) };

        traceW(L"Detect credentials file path is %s", confPath.c_str());

        const wchar_t* iniSection = mIniSection.c_str();
        const auto iniSectionA{ WC2MB(mIniSection) };

        // AWS �F�؏��
        std::string str_access_key_id;
        std::string str_secret_access_key;
        std::string str_region;

        GetIniStringA(confPathA, iniSectionA.c_str(), "aws_access_key_id", &str_access_key_id);
        GetIniStringA(confPathA, iniSectionA.c_str(), "aws_secret_access_key", &str_secret_access_key);
        GetIniStringA(confPathA, iniSectionA.c_str(), "region", &str_region);

        // ���W�X�g�� "HKLM:\SOFTWARE\Microsoft\Cryptography" ���� "MachineGuid" �̒l���擾
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

        // MachineGuid �̒l���L�[�ɂ��� keyid&secret �𕜍��� (�K�v�Ȃ�)
        if (!decryptIfNeed(secureKeyStr, &str_access_key_id))
        {
            traceW(L"%s: keyid decrypt fault", str_access_key_id.c_str());
        }

        if (!decryptIfNeed(secureKeyStr, &str_secret_access_key))
        {
            traceW(L"%s: secret decrypt fault", str_secret_access_key.c_str());
        }

        //
        // �o�P�b�g���t�B���^
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
        // �ő�\���o�P�b�g��
        //
        const int maxBuckets = (int)::GetPrivateProfileIntW(iniSection, L"max_buckets", -1, confPath.c_str());

        //
        // �ő�\���I�u�W�F�N�g��
        //
        const int maxObjects = (int)::GetPrivateProfileIntW(iniSection, L"max_objects", 1000, confPath.c_str());

        //
        // S3 �N���C�A���g�̐���
        //
        mSDKOptions = std::make_shared<Aws::SDKOptions>();
        APP_ASSERT(mSDKOptions);

        Aws::InitAPI(*mSDKOptions);

        Aws::Client::ClientConfiguration config;
        if (str_region.empty())
        {
            // �Ƃ肠�����f�t�H���g�E���[�W�����Ƃ��Đݒ肵�Ă���
            str_region = AWS_DEFAULT_REGION;
        }

        APP_ASSERT(!str_region.empty());

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
        }
        else
        {
            client = new Aws::S3::S3Client(config);
        }

        APP_ASSERT(client);

        //mClient.ptr = std::shared_ptr<Aws::S3::S3Client>(client);
        mClient.ptr = ClientPtr(client);

        //
        // �ڑ�����
        //
        const auto outcome = mClient.ptr->ListBuckets();
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: test ListBuckets");
            return false;
        }

        mWorkDirTime = STCTimeToWinFileTimeW(workDir);
        mWorkDir = workDir;
        mCacheDir = cacheDir;
        mMaxBuckets = maxBuckets;
        mMaxObjects = maxObjects;
        mRegion = MB2WC(str_region);

        ret = true;
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << "what: " << err.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "unknown error" << std::endl;
    }

    return ret;
}

void AwsS3::OnSvcStop()
{
    NEW_LOG_BLOCK();

    // AWS S3 �����I��

    if (mSDKOptions)
    {
        traceW(L"aws shutdown");
        Aws::ShutdownAPI(*mSDKOptions);
    }
}

struct ListBucketsTask : public ITask
{
    ICloudStorage* storage;

    ListBucketsTask(ICloudStorage* argStorage)
        : storage(argStorage) { }

    std::wstring synonymString()
    {
        return L"ListBucketsTask";
    }

    void run(CALLER_ARG IWorker* worker, const int indent) override
    {
        GetLogger()->traceW_impl(indent, __FUNCTIONW__, __LINE__, __FUNCTIONW__, L"call ListBuckets");

        storage->listBuckets(CONT_CALLER nullptr, {});
    }
};

struct IdleTask : public ITask
{
    AwsS3* s3;

    IdleTask(AwsS3* argThis) : s3(argThis) { }

    void run(CALLER_ARG IWorker* worker, const int indent) override
    {
        GetLogger()->traceW_impl(indent, __FUNCTIONW__, __LINE__, __FUNCTIONW__, L"on Idle");

        s3->OnIdleTime(CONT_CALLER0);
    }
};

void AwsS3::OnIdleTime(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    static int countCalled = 0;
    countCalled++;

    // IdleTask ����Ăяo����A��������t�@�C���̌Â����̂��폜

    namespace chrono = std::chrono;
    const auto now { chrono::system_clock::now() };

    //
    // �o�P�b�g�E�L���b�V��
    // 
    const auto lastSetTime = mBucketCache.getLastSetTime(CONT_CALLER0);

    if ((now - chrono::minutes(60)) > lastSetTime)
    {
        // �o�P�b�g�E�L���b�V�����쐬���Ă��� 60 ���ȏ�o��
        traceW(L"need re-load");

        // �o�P�b�g�̃L���b�V�����폜���āA�ēx�ꗗ���擾����
        mBucketCache.clear(CONT_CALLER0);

        // �o�P�b�g�ꗗ�̎擾 --> �L���b�V���̐���
        listBuckets(CONT_CALLER nullptr, {});
    }
    else
    {
        traceW(L"is valid");
    }

    //
    // �I�u�W�F�N�g�E�L���b�V��
    //

    // �ŏI�A�N�Z�X���� 5 ���ȏ�o�߂����I�u�W�F�N�g�E�L���b�V�����폜

    mObjectCache.deleteOldRecords(CONT_CALLER now - chrono::minutes(5));

    //
    // �t�@�C���E�L���b�V��
    //

    // �X�V�������� 24 ���Ԉȏ�o�߂����L���b�V���E�t�@�C�����폜����

    APP_ASSERT(std::filesystem::is_directory(mCacheDir));

    const auto nowMillis{ GetCurrentUtcMillis() };

    forEachFiles(mCacheDir, [this, nowMillis, &LOG_BLOCK()](const WIN32_FIND_DATA& wfd)
    {
        const auto lastAccessTime { WinFileTimeToUtcMillis(wfd.ftLastAccessTime) };

        traceW(L"cache file: [%s] [%s] lastAccess=%" PRIu64,
            wfd.cFileName, DecodeLocalNameToFileNameW(wfd.cFileName).c_str(), lastAccessTime);

        const auto diffMillis = nowMillis - lastAccessTime;
        if (diffMillis > (24ULL * 60 * 60 * 1000))
        {
            const auto delPath{ mCacheDir + L'\\' + wfd.cFileName };

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

    //
    // �e����̃��O
    //
    traceW(L"/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/");
    traceW(L"/");
    traceW(L"/         I  N  F  O  R  M  A  T  I  O  N  (%d)", countCalled);
    traceW(L"/");
    traceW(L"/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/");

    traceW(L"ClientPtr.RefCount=%d", mClient.ptr.getRefCount());

    traceW(L"[BucketCache]");
    mBucketCache.report(CONT_CALLER0);

    traceW(L"[ObjectCache]");
    mObjectCache.report(CONT_CALLER0);

#if _DEBUG
    const auto tid = ::GetCurrentThreadId();
    traceW(L"tid=%lu", tid);
#endif
}

bool AwsS3::OnPostSvcStart()
{
    // �o�P�b�g�ꗗ�̐�ǂ�
    // �����ł��Ȃ����D��x�͒Ⴂ
    mDelayedWorker->addTask(new ListBucketsTask{ this }, CanIgnore::NO, Priority::LOW);

    // �A�C�h�����̃��������(��)�̃^�X�N��o�^
    // �����ł��Ȃ����D��x�͒Ⴂ
    mIdleWorker->addTask(new IdleTask{ this }, CanIgnore::NO, Priority::LOW);

    return true;
}


// EOF