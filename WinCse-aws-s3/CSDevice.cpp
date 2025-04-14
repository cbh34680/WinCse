#include "CSDevice.hpp"

using namespace WCSE;


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

    return new CSDevice(argTempDir, argIniSection, workers);
}

CSDevice::~CSDevice()
{
    this->OnSvcStop();
}

struct ListBucketsTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::Yes; }
    Priority getPriority() const noexcept override { return Priority::Low; }

    CSDevice* mThat;

    ListBucketsTask(CSDevice* argThat)
        :
        mThat(argThat)
    {
    }

    std::wstring synonymString() const noexcept override
    {
        return L"ListBucketsTask";
    }

    void run(CALLER_ARG0) override
    {
        NEW_LOG_BLOCK();

        //traceW(L"call ListBuckets");

        mThat->listBuckets(CONT_CALLER nullptr);
    }
};

NTSTATUS CSDevice::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    const auto ntstatus = CSDeviceBase::OnSvcStart(argWorkDir, FileSystem);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: AwsS3A::OnSvcStart");
        return ntstatus;
    }

    // バケット一覧の先読み

    getWorker(L"delayed")->addTask(START_CALLER new ListBucketsTask{ this });

    return STATUS_SUCCESS;
}

NTSTATUS CSDevice::getHandleFromContext(CALLER_ARG
    WCSE::CSDeviceContext* argCSDCtx, DWORD argDesiredAccess, DWORD argCreationDisposition, PHANDLE pHandle)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDCtx);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    const auto remotePath{ ctx->mObjKey.str() };

    traceW(L"Context=%p ObjectKey=%s HANDLE=%p, RemotePath=%s DesiredAccess=%lu CreationDisposition=%lu",
        ctx, ctx->mObjKey.c_str(), ctx->mFile.handle(), remotePath.c_str(),
        argDesiredAccess, argCreationDisposition);

    // ファイル名への参照を登録

    UnprotectedShare<PrepareLocalFileShare> unsafeShare{ &mPrepareLocalFileShare, remotePath };  // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() 後の初回の呼び出し

            NTSTATUS ntstatus = ctx->openFileHandle(CONT_CALLER argDesiredAccess, argCreationDisposition);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: openFileHandle");
                return ntstatus;
            }

            APP_ASSERT(ctx->mFile.valid());
        }
    }   // 名前のロックを解除 (safeShare の生存期間)

    *pHandle = ctx->mFile.handle();

    return STATUS_SUCCESS;
}

// EOF
