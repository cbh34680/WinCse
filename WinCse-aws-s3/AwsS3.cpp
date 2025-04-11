#include "AwsS3.hpp"
#include <filesystem>

using namespace WCSE;

//
// AwsS3
//
WCSE::ICSDevice* NewCSDevice(PCWSTR argTempDir, PCWSTR argIniSection, NamedWorker argWorkers[])
{
    std::unordered_map<std::wstring, IWorker*> workers;

    if (NamedWorkersToMap(argWorkers, &workers) <= 0)
    {
        return nullptr;
    }

    for (const auto key: { L"delayed", L"timer", })
    {
        if (workers.find(key) == workers.end())
        {
            return nullptr;
        }
    }

    return new AwsS3(argTempDir, argIniSection, std::move(workers));
}

AwsS3::~AwsS3()
{
    // �f�X�g���N�^������Ă΂��̂ŁA�ē��\�Ƃ��Ă�������

    NEW_LOG_BLOCK();

    this->OnSvcStop();

    // �K�v�Ȃ����A�f�o�b�O���̃������E���[�N�����̎ז��ɂȂ�̂�

    clearListBucketsCache(START_CALLER0);
    clearObjectCache(START_CALLER0);
}

struct ListBucketsTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::Yes; }
    Priority getPriority() const noexcept override { return Priority::Low; }

    AwsS3* mAwsS3;

    ListBucketsTask(AwsS3* argAwsS3) : mAwsS3(argAwsS3) { }

    std::wstring synonymString() const noexcept override
    {
        return L"ListBucketsTask";
    }

    void run(CALLER_ARG0) override
    {
        NEW_LOG_BLOCK();

        //traceW(L"call ListBuckets");

        mAwsS3->listBuckets(CONT_CALLER nullptr);
    }
};

NTSTATUS AwsS3::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    StatsIncr(OnSvcStart);
    NEW_LOG_BLOCK();

    const auto ntstatus = AwsS3C::OnSvcStart(argWorkDir, FileSystem);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: AwsS3C::OnSvcStart");
        return ntstatus;
    }

    // �o�P�b�g�ꗗ�̐�ǂ�

    getWorker(L"delayed")->addTask(START_CALLER new ListBucketsTask{ this });

    return STATUS_SUCCESS;
}

VOID AwsS3::OnSvcStop()
{
    StatsIncr(OnSvcStop);

    AwsS3C::OnSvcStop();
}

void AwsS3::onIdle(CALLER_ARG0)
{
    AwsS3C::onIdle(CONT_CALLER0);

    //NEW_LOG_BLOCK();

    // IdleTask ����Ăяo����A��������t�@�C���̌Â����̂��폜

    const auto now{ std::chrono::system_clock::now() };

    // �o�P�b�g�E�L���b�V���̍č쐬

    this->reloadListBuckets(CONT_CALLER now - std::chrono::minutes(mSettings->bucketCacheExpiryMin));
}

NTSTATUS AwsS3::getHandleFromContext(CALLER_ARG
    WCSE::CSDeviceContext* argCSDeviceContext, DWORD argDesiredAccess, DWORD argCreationDisposition, PHANDLE pHandle)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    const auto remotePath{ ctx->mObjKey.str() };

    traceW(L"Context=%p ObjectKey=%s HANDLE=%p, RemotePath=%s DesiredAccess=%lu CreationDisposition=%lu",
        ctx, ctx->mObjKey.c_str(), ctx->mFile.handle(), remotePath.c_str(),
        argDesiredAccess, argCreationDisposition);

    // �t�@�C�����ւ̎Q�Ƃ�o�^

    UnprotectedShare<PrepareLocalFileShare> unsafeShare(&mPrepareLocalFileShare, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() ��̏���̌Ăяo��

            NTSTATUS ntstatus = ctx->openFileHandle(CONT_CALLER argDesiredAccess, argCreationDisposition);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: openFileHandle");
                return ntstatus;
            }

            APP_ASSERT(ctx->mFile.valid());
        }
    }   // ���O�̃��b�N������ (safeShare �̐�������)

    *pHandle = ctx->mFile.handle();

    return STATUS_SUCCESS;
}

//
// OpenContext
//
NTSTATUS OpenContext::openFileHandle(CALLER_ARG DWORD argDesiredAccess, DWORD argCreationDisposition)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(isFile());
    APP_ASSERT(mObjKey.meansFile());
    APP_ASSERT(mFile.invalid());

    const auto localPath{ getCacheFilePath() };

    const DWORD dwDesiredAccess = mGrantedAccess | argDesiredAccess;

    ULONG CreateFlags = 0;
    //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // �f�B���N�g���͑��삵�Ȃ�

    if (mCreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    HANDLE Handle = ::CreateFileW(localPath.c_str(),
        dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        argCreationDisposition, CreateFlags, 0);

    if (Handle == INVALID_HANDLE_VALUE)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    mFile = Handle;

    return STATUS_SUCCESS;
}

// EOF
