#include "AwsS3.hpp"
#include <iomanip>

using namespace WinCseLib;


struct ListBucketsTask : public ITask
{
    AwsS3* mS3;

    ListBucketsTask(AwsS3* that) : mS3(that) { }

    std::wstring synonymString()
    {
        return L"ListBucketsTask";
    }

    void run(CALLER_ARG0) override
    {
        NEW_LOG_BLOCK();

        traceW(L"call ListBuckets");

        mS3->listBuckets(CONT_CALLER nullptr, {});
    }
};

struct IdleTask : public ITask
{
    AwsS3* mS3;

    IdleTask(AwsS3* that) : mS3(that) { }

    void run(CALLER_ARG0) override
    {
        NEW_LOG_BLOCK();

        traceW(L"on Idle");

        mS3->OnIdleTime(CONT_CALLER0);
    }
};

static HANDLE gNotifEvent = NULL;
static bool gEndNotifWorker = false;
static std::thread* gNotifWorker = nullptr;

bool AwsS3::OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    StatsIncr(OnSvcStart);

    NEW_LOG_BLOCK();

    APP_ASSERT(argWorkDir);
    APP_ASSERT(FileSystem);

    bool ret = false;

    mFileSystem = FileSystem;

    // �o�P�b�g�ꗗ�̐�ǂ�
    // �����ł��ėD��x�͒Ⴂ
    mDelayedWorker->addTask(START_CALLER new ListBucketsTask{ this }, Priority::Low, CanIgnoreDuplicates::Yes);

    // �A�C�h�����̃��������(��)�̃^�X�N��o�^
    // �D��x�͒Ⴂ
    mIdleWorker->addTask(START_CALLER new IdleTask{ this }, Priority::Low, CanIgnoreDuplicates::None);

    // �O������̒ʒm�҂��C�x���g�̐���
    SECURITY_ATTRIBUTES sa{ 0 };
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);

    // �Z�L�����e�B�L�q�q�̍쐬
    PSECURITY_DESCRIPTOR pSD = (PSECURITY_DESCRIPTOR)::LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!pSD)
    {
        traceW(L"fault: LocalAlloc");
        goto exit;
    }

    if (!::InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION))
    {
        traceW(L"fault: InitializeSecurityDescriptor");
        goto exit;
    }

    // ���ׂẴ��[�U�[�Ƀt���A�N�Z�X������
#pragma warning(suppress: 6248)
    if (!::SetSecurityDescriptorDacl(pSD, TRUE, NULL, FALSE))
    {
        traceW(L"fault: SetSecurityDescriptorDacl");
        goto exit;
    }

    sa.lpSecurityDescriptor = pSD;

    gNotifEvent = ::CreateEventW(&sa, FALSE, FALSE, L"Global\\WinCse-AwsS3-cache-report");
    if (!gNotifEvent)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateEvent error=%ld", lerr);
        return false;
    }

    gNotifWorker = new std::thread(&AwsS3::notifListener, this);
    APP_ASSERT(gNotifWorker);

    ::SetThreadDescription(gNotifWorker->native_handle(), L"WinCse::notifListener");

    ret = true;

exit:
    if (pSD)
    {
        ::LocalFree(pSD);
        pSD = NULL;
    }

    return ret;
}

void AwsS3::OnSvcStop()
{
    StatsIncr(OnSvcStop);

    NEW_LOG_BLOCK();

    // �f�X�g���N�^������Ă΂��̂ŁA�ē��\�Ƃ��Ă�������

    if (gNotifEvent)
    {
        gEndNotifWorker = true;
        const auto b = ::SetEvent(gNotifEvent);
        APP_ASSERT(b);
    }

    if (gNotifWorker)
    {
        gNotifWorker->join();
        delete gNotifWorker;
        gNotifWorker = nullptr;
    }

    if (gNotifEvent)
    {
        ::CloseHandle(gNotifEvent);
        gNotifEvent = NULL;
    }

    // AWS S3 �����I��

    if (mSDKOptions)
    {
        traceW(L"aws shutdown");
        Aws::ShutdownAPI(*mSDKOptions);

        mSDKOptions.reset();
    }
}

void AwsS3::notifListener()
{
    NEW_LOG_BLOCK();

    while (true)
    {
        const auto reason = ::WaitForSingleObject(gNotifEvent, INFINITE);
        if (reason != WAIT_OBJECT_0)
        {
            const auto lerr = ::GetLastError();
            traceW(L"un-expected reason=%lu, lerr=%lu, break", reason, lerr);
            break;
        }

        if (gEndNotifWorker)
        {
            traceW(L"catch end-thread request, break");
            break;
        }

        //
        // �e����̃��O
        //
        SYSTEMTIME st;
        ::GetLocalTime(&st);

        std::wstringstream ss;
        ss << mCacheReportDir;
        ss << L'\\';
        ss << L"report";
        ss << L'-';
        ss << std::setw(4) << std::setfill(L'0') << st.wYear;
        ss << std::setw(2) << std::setfill(L'0') << st.wMonth;
        ss << std::setw(2) << std::setfill(L'0') << st.wDay;
        ss << L'-';
        ss << std::setw(2) << std::setfill(L'0') << st.wHour;
        ss << std::setw(2) << std::setfill(L'0') << st.wMinute;
        ss << std::setw(2) << std::setfill(L'0') << st.wSecond;
        ss << L".log";

        const auto path{ ss.str() };

        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"wt") == 0)
        {
            fwprintf(fp, L"ClientPtr.RefCount=%d\n", mClient.ptr.getRefCount());

            fwprintf(fp, L"[BucketCache]\n");
            this->reportBucketCache(START_CALLER fp);

            fwprintf(fp, L"[ObjectCache]\n");
            this->reportObjectCache(START_CALLER fp);

            fclose(fp);
            fp = nullptr;
        }
    }

    traceW(L"thread end");
}

// EOF