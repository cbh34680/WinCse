#include "CSDriverBase.hpp"

using namespace CSELIB;
using namespace CSEDRV;


NTSTATUS CSDriverBase::RelayGetSecurityByName(PCWSTR argFileName, PUINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize)
{
    StatsIncr(RelayGetSecurityByName);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, argFileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->GetSecurityByName(argFileName, argFileAttributes, argSecurityDescriptor, argSecurityDescriptorSize);
    }

    return ntstatus;
}

#define RETURN_IF_READONLY()		if (mRuntimeEnv->ReadOnly) return STATUS_ACCESS_DENIED

#define FLAG_NAME(name)             FCTX_FLAGS_ ## name

#define SET_FLAG(ctx, name)         (ctx)->mFlags |= FLAG_NAME(name)
#define SET_FLAG_IF(s, ctx, name)   if (NT_SUCCESS((s))) SET_FLAG(ctx, name)

#define SET_DEFAULT_FA_IF(s, p)     if (NT_SUCCESS((s))) this->applyDefaultFileAttributes(p)

#define RETURN_IF_NOT_ALLOWED_VOID(ctx, ...) \
do { \
    const FileTypeEnum arr[] = { __VA_ARGS__ }; \
    if (std::find(std::begin(arr), std::end(arr), (ctx)->getDirEntry()->mFileType) == std::end(arr)) \
        return; \
} while (0)

#define RETURN_IF_NOT_ALLOWED(ctx, ...) \
do { \
    const FileTypeEnum arr[] = { __VA_ARGS__ }; \
    if (std::find(std::begin(arr), std::end(arr), (ctx)->getDirEntry()->mFileType) == std::end(arr)) \
        return STATUS_ACCESS_DENIED; \
} while (0)


NTSTATUS CSDriverBase::RelayOpen(PCWSTR argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelayOpen);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, argWinPath };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->Open(argWinPath, argCreateOptions, argGrantedAccess, (FileContext**)argFileContext, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);

    if (NT_SUCCESS(ntstatus))
    {
        FileContext* ctx = dynamic_cast<FileContext*>(*argFileContext);
        APP_ASSERT(ctx);

        mFileContextSweeper.add(ctx);

        SET_FLAG_IF(ntstatus, ctx, OPEN);
    }

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayCreate(PCWSTR argFileName, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelayCreate);
    RETURN_IF_READONLY();

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, argFileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->Create(argFileName, argCreateOptions, argGrantedAccess, argFileAttributes, argSecurityDescriptor, argAllocationSize, (FileContext**)argFileContext, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);

    if (NT_SUCCESS(ntstatus))
    {
        FileContext* ctx = dynamic_cast<FileContext*>(*argFileContext);
        APP_ASSERT(ctx);

        mFileContextSweeper.add(ctx);

        SET_FLAG_IF(ntstatus, ctx, CREATE);
    }

    return ntstatus;
}

VOID CSDriverBase::RelayClose(IFileContext* argFileContext)
{
    StatsIncr(RelayClose);

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    mFileContextSweeper.remove(ctx);

    SET_FLAG(ctx, CLOSE);

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        this->Close(ctx);

        // delete Ç≥ÇÍÇÈÇÃÇ≈ÅAÇ±Ç±à»ç~ÇÕ ctx ÇÕégÇ¶Ç»Ç¢
    }
}

VOID CSDriverBase::RelayCleanup(IFileContext* argFileContext, PWSTR argFileName, ULONG argFlags)
{
    StatsIncr(RelayCleanup);

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED_VOID(ctx, FileTypeEnum::Directory, FileTypeEnum::File);

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        this->Cleanup(ctx, argFileName, argFlags);
    }

    SET_FLAG(ctx, CLEANUP);
}

NTSTATUS CSDriverBase::RelayFlush(IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelayFlush);

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->Flush(ctx, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);
    SET_FLAG_IF(ntstatus, ctx, FLUSH);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayGetFileInfo(IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelayGetFileInfo);

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->GetFileInfo(ctx, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);
    SET_FLAG_IF(ntstatus, ctx, GET_FILE_INFO);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayGetSecurity(IFileContext* argFileContext, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize)
{
    StatsIncr(RelayGetSecurity);

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->GetSecurity(ctx, argSecurityDescriptor, argSecurityDescriptorSize);
    }

    SET_FLAG_IF(ntstatus, ctx, GET_SECURITY);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayOverwrite(IFileContext* argFileContext, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelayOverwrite);
    RETURN_IF_READONLY();

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->Overwrite(ctx, argFileAttributes, argReplaceFileAttributes, argAllocationSize, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);
    SET_FLAG_IF(ntstatus, ctx, OVERWRITE);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayRead(IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred)
{
    StatsIncr(RelayRead);

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->Read(ctx, argBuffer, argOffset, argLength, argBytesTransferred);
    }

    SET_FLAG_IF(ntstatus, ctx, READ);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayReadDirectory(IFileContext* argFileContext, PWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred)
{
    StatsIncr(RelayReadDirectory);

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::Root, FileTypeEnum::Bucket, FileTypeEnum::Directory);

    // ReadDirectory() Ç≈ÇÕ ctx->mWinPath à»äOÇ…ÅAÉfÉBÉåÉNÉgÉäÇÃíÜÇÃÉtÉ@ÉCÉãÇ
    // àµÇ§ÇΩÇﬂÅAÇ±Ç±Ç≈îrëºêßå‰ÇπÇ∏ÉtÉ@ÉCÉãñºÇ≤Ç∆Ç…ä÷êîì‡ïîÇ≈îrëºêßå‰ÇµÇƒÇ¢ÇÈ

    NTSTATUS ntstatus = this->ReadDirectory(ctx, argPattern, argMarker, argBuffer, argBufferLength, argBytesTransferred);

    SET_FLAG_IF(ntstatus, ctx, READ_DIRECTORY);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayRename(IFileContext* argFileContext, PWSTR argFileName, PWSTR argNewFileName, BOOLEAN argReplaceIfExists)
{
    StatsIncr(RelayRename);
    RETURN_IF_READONLY();

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::Directory, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        UnprotectedShare<FileNameGuard> unsafeShare2{ &mFileNameGuard, argNewFileName };
        {
            const auto safeShare2{ unsafeShare2.lock() };

            ntstatus = this->Rename(ctx, argFileName, argNewFileName, argReplaceIfExists);
        }
    }

    SET_FLAG_IF(ntstatus, ctx, RENAME);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelaySetBasicInfo(IFileContext* argFileContext, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelaySetBasicInfo);
    RETURN_IF_READONLY();

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::Directory, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->SetBasicInfo(ctx, argFileAttributes, argCreationTime, argLastAccessTime, argLastWriteTime, argChangeTime, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);
    SET_FLAG_IF(ntstatus, ctx, SET_BASIC_INFO);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelaySetFileSize(IFileContext* argFileContext, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelaySetFileSize);
    RETURN_IF_READONLY();

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->SetFileSize(ctx, argNewSize, argSetAllocationSize, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);
    SET_FLAG_IF(ntstatus, ctx, SET_FILE_SIZE);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelaySetSecurity(IFileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor)
{
    StatsIncr(RelaySetSecurity);
    RETURN_IF_READONLY();

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::Directory, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->SetSecurity(ctx, argSecurityInformation, argModificationDescriptor);
    }

    SET_FLAG_IF(ntstatus, ctx, SET_SECURITY);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayWrite(IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(RelayWrite);
    RETURN_IF_READONLY();

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->Write(ctx, argBuffer, argOffset, argLength, argWriteToEndOfFile, argConstrainedIo, argBytesTransferred, argFileInfo);
    }

    SET_DEFAULT_FA_IF(ntstatus, argFileInfo);
    SET_FLAG_IF(ntstatus, ctx, WRITE);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelaySetDelete(IFileContext* argFileContext, PWSTR argFileName, BOOLEAN argDeleteFile)
{
    StatsIncr(RelaySetDelete);
    RETURN_IF_READONLY();

    FileContext* ctx = dynamic_cast<FileContext*>(argFileContext);
    APP_ASSERT(ctx);

    RETURN_IF_NOT_ALLOWED(ctx, FileTypeEnum::Directory, FileTypeEnum::File);

    NTSTATUS ntstatus = STATUS_INVALID_DEVICE_REQUEST;

    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
    {
        const auto safeShare{ unsafeShare.lock() };

        ntstatus = this->SetDelete(ctx, argFileName, argDeleteFile);
    }

    SET_FLAG_IF(ntstatus, ctx, SET_DELETE);

    return ntstatus;
}

NTSTATUS CSDriverBase::RelayPreCreateFilesystem(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* argVolumeParams)
{
    StatsIncr(RelayPreCreateFilesystem);
    NEW_LOG_BLOCK();

    // PreCreateFilesystem() ÇÃì`îd

    for (auto* service: mServices)
    {
        const auto klassName{ getDerivedClassNamesW(service) };

        traceW(L"%s::PreCreateFilesystem()", klassName.c_str());

        const auto ntstatus = service->PreCreateFilesystem(Service, argWorkDir, argVolumeParams);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: %s::PreCreateFilesystem", klassName.c_str());
            return ntstatus;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS CSDriverBase::RelayOnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    StatsIncr(RelayOnSvcStart);
    NEW_LOG_BLOCK();

    // OnSvcStart() ÇÃì`îd

    for (auto* service: mServices)
    {
        const auto klassName{ getDerivedClassNamesW(service) };

        traceW(L"%s::OnSvcStart()", klassName.c_str());

        const auto ntstatus = service->OnSvcStart(argWorkDir, FileSystem);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: OnSvcStart");
            return ntstatus;
        }
    }

    return STATUS_SUCCESS;
}

VOID CSDriverBase::RelayOnSvcStop()
{
    StatsIncr(RelayOnSvcStop);

    NEW_LOG_BLOCK();

    // OnSvcStop() ÇÃì`îd

    for (auto* service: mServices)
    {
        const auto klassName{ getDerivedClassNamesW(service) };

        traceW(L"%s::OnSvcStop()", klassName.c_str());

        service->OnSvcStop();
    }
}

// EOF