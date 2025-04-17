#include "CSDriverBase.hpp""

using namespace WCSE;


NTSTATUS CSDriverBase::DoGetSecurityByName(PCWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->GetSecurityByName(FileName, PFileAttributes, SecurityDescriptor, PSecurityDescriptorSize);
    }
}

NTSTATUS CSDriverBase::DoOpen(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->Open(FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);
    }
}

NTSTATUS CSDriverBase::DoCreate(PCWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->Create(FileName, CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);
    }
}

VOID CSDriverBase::DoClose(PTFS_FILE_CONTEXT* FileContext)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        this->Close(FileContext);
    }
}

VOID CSDriverBase::DoCleanup(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, ULONG Flags)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        this->Cleanup(FileContext, FileName, Flags);
    }
}

NTSTATUS CSDriverBase::DoFlush(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->Flush(FileContext, FileInfo);
    }
}

NTSTATUS CSDriverBase::DoGetFileInfo(PTFS_FILE_CONTEXT* FileContext, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->GetFileInfo(FileContext, FileInfo);
    }
}

NTSTATUS CSDriverBase::DoGetSecurity(PTFS_FILE_CONTEXT* FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, PSIZE_T PSecurityDescriptorSize)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->GetSecurity(FileContext, SecurityDescriptor, PSecurityDescriptorSize);
    }
}

NTSTATUS CSDriverBase::DoOverwrite(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->Overwrite(FileContext, FileAttributes, ReplaceFileAttributes, AllocationSize, FileInfo);
    }
}

NTSTATUS CSDriverBase::DoRead(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->Read(FileContext, Buffer, Offset, Length, PBytesTransferred);
    }
}

NTSTATUS CSDriverBase::DoReadDirectory(PTFS_FILE_CONTEXT* FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->ReadDirectory(FileContext, Pattern, Marker, Buffer, BufferLength, PBytesTransferred);
    }
}

NTSTATUS CSDriverBase::DoRename(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->Rename(FileContext, FileName, NewFileName, ReplaceIfExists);
    }
}

NTSTATUS CSDriverBase::DoSetBasicInfo(PTFS_FILE_CONTEXT* FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->SetBasicInfo(FileContext, FileAttributes, CreationTime, LastAccessTime, LastWriteTime, ChangeTime, FileInfo);
    }
}

NTSTATUS CSDriverBase::DoSetFileSize(PTFS_FILE_CONTEXT* FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->SetFileSize(FileContext, NewSize, SetAllocationSize, FileInfo);
    }
}

NTSTATUS CSDriverBase::DoSetSecurity(PTFS_FILE_CONTEXT* FileContext, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->SetSecurity(FileContext, SecurityInformation, ModificationDescriptor);
    }
}

NTSTATUS CSDriverBase::DoWrite(PTFS_FILE_CONTEXT* FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->Write(FileContext, Buffer, Offset, Length, WriteToEndOfFile, ConstrainedIo, PBytesTransferred, FileInfo);
    }
}

NTSTATUS CSDriverBase::DoSetDelete(PTFS_FILE_CONTEXT* FileContext, PWSTR FileName, BOOLEAN deleteFile)
{
    UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, FileContext->FileName };
    {
        const auto safeShare{ unsafeShare.lock() };

        return this->SetDelete( FileContext, FileName, deleteFile);
    }
}

// EOF