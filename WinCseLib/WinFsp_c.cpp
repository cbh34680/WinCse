/**
 * @file passthrough.c
 *
 * @copyright 2015-2024 Bill Zissimopoulos
 */
/*
 * This file is part of WinFsp.
 *
 * You can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 3 as published by the Free Software
 * Foundation.
 *
 * Licensees holding a valid commercial license may use this software
 * in accordance with the commercial license agreement provided in
 * conjunction with the software.  The terms and conditions of any such
 * commercial license agreement shall govern, supersede, and render
 * ineffective any application of the GPLv3 license to this software,
 * notwithstanding of any reference thereto in the software or
 * associated repository.
 */
#pragma warning(disable: 4100)

#include "WinCseLib.h"

//#include <winfsp/winfsp.h>
#include <strsafe.h>

//#define PROGNAME                        "passthrough"
WCHAR* PROGNAME;
//#define ALLOCATION_UNIT                 4096
#define FULLPATH_SIZE                   (MAX_PATH + FSP_FSCTL_TRANSACT_PATH_SIZEMAX / sizeof(WCHAR))
/*
#define info(format, ...)               FspServiceLog(EVENTLOG_INFORMATION_TYPE, format, __VA_ARGS__)
#define warn(format, ...)               FspServiceLog(EVENTLOG_WARNING_TYPE, format, __VA_ARGS__)
#define fail(format, ...)               FspServiceLog(EVENTLOG_ERROR_TYPE, format, __VA_ARGS__)
*/
#define info(format, ...)               { WCHAR* p=_wcsdup(format); if (p) { FspServiceLog(EVENTLOG_INFORMATION_TYPE, p, __VA_ARGS__); free(p); } }
#define warn(format, ...)               { WCHAR* p=_wcsdup(format); if (p) { FspServiceLog(EVENTLOG_WARNING_TYPE, p, __VA_ARGS__); free(p); } }
#define fail(format, ...)               { WCHAR* p=_wcsdup(format); if (p) { FspServiceLog(EVENTLOG_ERROR_TYPE, p, __VA_ARGS__); free(p); } }


#define ConcatPath(Ptfs, FN, FP)        (0 == StringCbPrintfW(FP, sizeof FP, L"%s%s", Ptfs->Path, FN))
#define HandleFromContext(FC)           (((PTFS_FILE_CONTEXT *)(FC))->Handle)

typedef struct
{
    FSP_FILE_SYSTEM *FileSystem;
    PWSTR Path;
} PTFS;

/*
typedef struct
{
    HANDLE Handle;
    PVOID DirBuffer;
} PTFS_FILE_CONTEXT;
*/

WINFSP_IF* gWinFspIf;
#define StatsIncr(fname)    if (gWinFspIf) InterlockedIncrement(& (gWinFspIf->stats.fname))

#if !WINFSP_PASSTHROUGH
WinCseLib::ICSDriver* CSDriver()
{
    ::SetLastError(ERROR_SUCCESS);

    return gWinFspIf->pDriver;
}
#endif

static const FSP_FILE_SYSTEM_INTERFACE* getPtfsInterface();

/*static*/ NTSTATUS GetFileInfoInternal(HANDLE Handle, FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(GetFileInfoInternal);

#if !WINFSP_PASSTHROUGH
    APP_ASSERT(Handle);
    APP_ASSERT(Handle != INVALID_HANDLE_VALUE);
#endif

    BY_HANDLE_FILE_INFORMATION ByHandleFileInfo = { 0 };

    if (!GetFileInformationByHandle(Handle, &ByHandleFileInfo))
        return FspNtStatusFromWin32(GetLastError());

    FileInfo->FileAttributes = ByHandleFileInfo.dwFileAttributes;
    FileInfo->ReparseTag = 0;
    FileInfo->FileSize =
        ((UINT64)ByHandleFileInfo.nFileSizeHigh << 32) | (UINT64)ByHandleFileInfo.nFileSizeLow;
    FileInfo->AllocationSize = (FileInfo->FileSize + ALLOCATION_UNIT - 1)
        / ALLOCATION_UNIT * ALLOCATION_UNIT;
    FileInfo->CreationTime = ((PLARGE_INTEGER)&ByHandleFileInfo.ftCreationTime)->QuadPart;
    FileInfo->LastAccessTime = ((PLARGE_INTEGER)&ByHandleFileInfo.ftLastAccessTime)->QuadPart;
    FileInfo->LastWriteTime = ((PLARGE_INTEGER)&ByHandleFileInfo.ftLastWriteTime)->QuadPart;
    FileInfo->ChangeTime = FileInfo->LastWriteTime;
    FileInfo->IndexNumber =
        ((UINT64)ByHandleFileInfo.nFileIndexHigh << 32) | (UINT64)ByHandleFileInfo.nFileIndexLow;
    FileInfo->HardLinks = 0;

    return STATUS_SUCCESS;
}

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM *FileSystem,
    FSP_FSCTL_VOLUME_INFO *VolumeInfo)
{
    StatsIncr(GetVolumeInfo);

    PTFS* Ptfs = (PTFS*)FileSystem->UserContext;
    WCHAR Root[MAX_PATH] = { 0 };
    ULARGE_INTEGER TotalSize = { 0 }, FreeSize = { 0 };

    if (!GetVolumePathName(Ptfs->Path, Root, MAX_PATH))
        return FspNtStatusFromWin32(GetLastError());

    if (!GetDiskFreeSpaceEx(Root, 0, &TotalSize, &FreeSize))
        return FspNtStatusFromWin32(GetLastError());

    VolumeInfo->TotalSize = TotalSize.QuadPart;
    VolumeInfo->FreeSize = FreeSize.QuadPart;

    return STATUS_SUCCESS;
}

static NTSTATUS SetVolumeLabel_(FSP_FILE_SYSTEM *FileSystem,
    PWSTR VolumeLabel,
    FSP_FSCTL_VOLUME_INFO *VolumeInfo)
{
    StatsIncr(SetVolumeLabel_);

    /* we do not support changing the volume label */
    return STATUS_INVALID_DEVICE_REQUEST;
}

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM *FileSystem,
    PWSTR FileName, PUINT32 PFileAttributes,
    PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize)
{
    StatsIncr(GetSecurityByName);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoGetSecurityByName(FileName, PFileAttributes, SecurityDescriptor, PSecurityDescriptorSize);

#else
    PTFS* Ptfs = (PTFS*)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 };
    HANDLE Handle = INVALID_HANDLE_VALUE;
    FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo = { 0 };
    DWORD SecurityDescriptorSizeNeeded = 0;
    NTSTATUS Result = STATUS_UNSUCCESSFUL;

    if (!ConcatPath(Ptfs, FileName, FullPath))
        return STATUS_OBJECT_NAME_INVALID;

    Handle = CreateFileW(FullPath,
        FILE_READ_ATTRIBUTES | READ_CONTROL, 0, 0,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (INVALID_HANDLE_VALUE == Handle)
    {
        DWORD lerr = GetLastError();
        Result = FspNtStatusFromWin32(lerr);
        goto exit;
    }

    if (0 != PFileAttributes)
    {
        if (!GetFileInformationByHandleEx(Handle,
            FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
        {
            Result = FspNtStatusFromWin32(GetLastError());
            goto exit;
        }

        *PFileAttributes = AttributeTagInfo.FileAttributes;
    }

    if (0 != PSecurityDescriptorSize)
    {
        if (!GetKernelObjectSecurity(Handle,
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            SecurityDescriptor, (DWORD)*PSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
        {
            *PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
            Result = FspNtStatusFromWin32(GetLastError());
            goto exit;
        }

        *PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
    }

    Result = STATUS_SUCCESS;

exit:
    if (INVALID_HANDLE_VALUE != Handle)
        CloseHandle(Handle);

    return Result;
#endif
}

static NTSTATUS Create(FSP_FILE_SYSTEM *FileSystem,
    PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
    UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize,
    PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(Create);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoCreate(FileName, CreateOptions, GrantedAccess, FileAttributes, SecurityDescriptor, AllocationSize, PFileContext, FileInfo);

#else
    PTFS* Ptfs = (PTFS*)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 };
    SECURITY_ATTRIBUTES SecurityAttributes = { 0 };
    ULONG CreateFlags = 0;
    PTFS_FILE_CONTEXT* FileContext = nullptr;

    if (!ConcatPath(Ptfs, FileName, FullPath))
        return STATUS_OBJECT_NAME_INVALID;

    FileContext = (PTFS_FILE_CONTEXT*)malloc(sizeof *FileContext);
    if (0 == FileContext)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(FileContext, 0, sizeof *FileContext);

    SecurityAttributes.nLength = sizeof SecurityAttributes;
    SecurityAttributes.lpSecurityDescriptor = SecurityDescriptor;
    SecurityAttributes.bInheritHandle = FALSE;

    CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
    if (CreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    if (CreateOptions & FILE_DIRECTORY_FILE)
    {
        /*
         * It is not widely known but CreateFileW can be used to create directories!
         * It requires the specification of both FILE_FLAG_BACKUP_SEMANTICS and
         * FILE_FLAG_POSIX_SEMANTICS. It also requires that FileAttributes has
         * FILE_ATTRIBUTE_DIRECTORY set.
         */
        CreateFlags |= FILE_FLAG_POSIX_SEMANTICS;
        FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    }
    else
        FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

    if (0 == FileAttributes)
        FileAttributes = FILE_ATTRIBUTE_NORMAL;

    FileContext->Handle = CreateFileW(FullPath,
        GrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &SecurityAttributes,
        CREATE_NEW, CreateFlags | FileAttributes, 0);
    if (INVALID_HANDLE_VALUE == FileContext->Handle)
    {
        free(FileContext);

        DWORD lerr = GetLastError();
        return FspNtStatusFromWin32(lerr);
    }

    *PFileContext = FileContext;

    return GetFileInfoInternal(FileContext->Handle, FileInfo);
#endif
}

static NTSTATUS Open(FSP_FILE_SYSTEM *FileSystem,
    PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess,
    PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(Open);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoOpen(FileName, CreateOptions, GrantedAccess, PFileContext, FileInfo);

#else
    PTFS *Ptfs = (PTFS *)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 };
    ULONG CreateFlags = 0;
    PTFS_FILE_CONTEXT *FileContext = nullptr;

    if (!ConcatPath(Ptfs, FileName, FullPath))
        return STATUS_OBJECT_NAME_INVALID;

    FileContext = (PTFS_FILE_CONTEXT*)malloc(sizeof *FileContext);
    if (0 == FileContext)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(FileContext, 0, sizeof *FileContext);

    CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
    if (CreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    FileContext->Handle = CreateFileW(FullPath,
        GrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        OPEN_EXISTING, CreateFlags, 0);
    if (INVALID_HANDLE_VALUE == FileContext->Handle)
    {
        free(FileContext);
        return FspNtStatusFromWin32(GetLastError());
    }

    *PFileContext = FileContext;

    return GetFileInfoInternal(FileContext->Handle, FileInfo);
#endif
}

static NTSTATUS Overwrite(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(Overwrite);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoOverwrite((PTFS_FILE_CONTEXT*)FileContext,
        FileAttributes, ReplaceFileAttributes, AllocationSize, FileInfo);

#else
    HANDLE Handle = HandleFromContext(FileContext);
    FILE_BASIC_INFO BasicInfo = { 0 };
    FILE_ALLOCATION_INFO AllocationInfo = { 0 };
    FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo = { 0 };

    if (ReplaceFileAttributes)
    {
        if (0 == FileAttributes)
            FileAttributes = FILE_ATTRIBUTE_NORMAL;

        BasicInfo.FileAttributes = FileAttributes;
        if (!SetFileInformationByHandle(Handle,
            FileBasicInfo, &BasicInfo, sizeof BasicInfo))
            return FspNtStatusFromWin32(GetLastError());
    }
    else if (0 != FileAttributes)
    {
        if (!GetFileInformationByHandleEx(Handle,
            FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
            return FspNtStatusFromWin32(GetLastError());

        BasicInfo.FileAttributes = FileAttributes | AttributeTagInfo.FileAttributes;
        if (BasicInfo.FileAttributes ^ FileAttributes)
        {
            if (!SetFileInformationByHandle(Handle,
                FileBasicInfo, &BasicInfo, sizeof BasicInfo))
                return FspNtStatusFromWin32(GetLastError());
        }
    }

    if (!SetFileInformationByHandle(Handle,
        FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, FileInfo);
#endif
}

static VOID Cleanup(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext, PWSTR FileName, ULONG Flags)
{
    StatsIncr(Cleanup);

#if !WINFSP_PASSTHROUGH
    CSDriver()->DoCleanup((PTFS_FILE_CONTEXT*)FileContext, FileName, Flags);

#else
    HANDLE Handle = HandleFromContext(FileContext);

    if (Flags & FspCleanupDelete)
    {
        CloseHandle(Handle);

        /* this will make all future uses of Handle to fail with STATUS_INVALID_HANDLE */
        HandleFromContext(FileContext) = INVALID_HANDLE_VALUE;
    }
#endif
}

static VOID Close(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext0)
{
    StatsIncr(Close);

    PTFS_FILE_CONTEXT *FileContext = (PTFS_FILE_CONTEXT*)FileContext0;

#if !WINFSP_PASSTHROUGH
    CSDriver()->DoClose(FileContext);

#else
    HANDLE Handle = HandleFromContext(FileContext);

    CloseHandle(Handle);

    FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);
    free(FileContext);
#endif
}

static NTSTATUS Read(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
    PULONG PBytesTransferred)
{
    StatsIncr(Read);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoRead((PTFS_FILE_CONTEXT*)FileContext, Buffer, Offset, Length, PBytesTransferred);

#else
    HANDLE Handle = HandleFromContext(FileContext);
    OVERLAPPED Overlapped = { 0 };

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!ReadFile(Handle, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        return FspNtStatusFromWin32(GetLastError());
    }

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS Write(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length,
    BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(Write);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoWrite((PTFS_FILE_CONTEXT*)FileContext, Buffer, Offset, Length, WriteToEndOfFile, ConstrainedIo, PBytesTransferred, FileInfo);

#else
    HANDLE Handle = HandleFromContext(FileContext);
    LARGE_INTEGER FileSize = { 0 };
    OVERLAPPED Overlapped = { 0 };

    if (ConstrainedIo)
    {
        if (!GetFileSizeEx(Handle, &FileSize))
            return FspNtStatusFromWin32(GetLastError());

        if (Offset >= (UINT64)FileSize.QuadPart)
            return STATUS_SUCCESS;
        if (Offset + Length > (UINT64)FileSize.QuadPart)
            Length = (ULONG)((UINT64)FileSize.QuadPart - Offset);
    }

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!WriteFile(Handle, Buffer, Length, PBytesTransferred, &Overlapped))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, FileInfo);
#endif
}

static NTSTATUS Flush(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(Flush);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoFlush((PTFS_FILE_CONTEXT*)FileContext, FileInfo);

#else
    HANDLE Handle = HandleFromContext(FileContext);

    /* we do not flush the whole volume, so just return SUCCESS */
    if (0 == Handle)
        return STATUS_SUCCESS;

    if (!FlushFileBuffers(Handle))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, FileInfo);
#endif
}

static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext0,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(GetFileInfo);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoGetFileInfo((PTFS_FILE_CONTEXT*)FileContext0, FileInfo);

#else
    HANDLE Handle = HandleFromContext(FileContext0);

    return GetFileInfoInternal(Handle, FileInfo);
#endif
}

static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext, UINT32 FileAttributes,
    UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(SetBasicInfo);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoSetBasicInfo((PTFS_FILE_CONTEXT*)FileContext, FileAttributes,
        CreationTime, LastAccessTime, LastWriteTime, ChangeTime, FileInfo);

#else
    HANDLE Handle = HandleFromContext(FileContext);

    FILE_BASIC_INFO BasicInfo = { 0 };

    if (INVALID_FILE_ATTRIBUTES == FileAttributes)
        FileAttributes = 0;
    else if (0 == FileAttributes)
        FileAttributes = FILE_ATTRIBUTE_NORMAL;

    BasicInfo.FileAttributes = FileAttributes;
    BasicInfo.CreationTime.QuadPart = CreationTime;
    BasicInfo.LastAccessTime.QuadPart = LastAccessTime;
    BasicInfo.LastWriteTime.QuadPart = LastWriteTime;
    //BasicInfo.ChangeTime = ChangeTime;

    if (!SetFileInformationByHandle(Handle,
        FileBasicInfo, &BasicInfo, sizeof BasicInfo))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, FileInfo);
#endif
}

static NTSTATUS SetFileSize(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize,
    FSP_FSCTL_FILE_INFO *FileInfo)
{
    StatsIncr(SetFileSize);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoSetFileSize((PTFS_FILE_CONTEXT*)FileContext, NewSize, SetAllocationSize, FileInfo);

#else
    HANDLE Handle = HandleFromContext(FileContext);
    FILE_ALLOCATION_INFO AllocationInfo = { 0 };
    FILE_END_OF_FILE_INFO EndOfFileInfo = { 0 };

    if (SetAllocationSize)
    {
        /*
         * This file system does not maintain AllocationSize, although NTFS clearly can.
         * However it must always be FileSize <= AllocationSize and NTFS will make sure
         * to truncate the FileSize if it sees an AllocationSize < FileSize.
         *
         * If OTOH a very large AllocationSize is passed, the call below will increase
         * the AllocationSize of the underlying file, although our file system does not
         * expose this fact. This AllocationSize is only temporary as NTFS will reset
         * the AllocationSize of the underlying file when it is closed.
         */

        AllocationInfo.AllocationSize.QuadPart = NewSize;

        if (!SetFileInformationByHandle(Handle,
            FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
            return FspNtStatusFromWin32(GetLastError());
    }
    else
    {
        EndOfFileInfo.EndOfFile.QuadPart = NewSize;

        if (!SetFileInformationByHandle(Handle,
            FileEndOfFileInfo, &EndOfFileInfo, sizeof EndOfFileInfo))
            return FspNtStatusFromWin32(GetLastError());
    }

    return GetFileInfoInternal(Handle, FileInfo);
#endif
}

static NTSTATUS Rename(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext,
    PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists)
{
    StatsIncr(Rename);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoRename((PTFS_FILE_CONTEXT*)FileContext, FileName, NewFileName, ReplaceIfExists);

#else
    PTFS* Ptfs = (PTFS*)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 }, NewFullPath[FULLPATH_SIZE] = { 0 };

    if (!ConcatPath(Ptfs, FileName, FullPath))
        return STATUS_OBJECT_NAME_INVALID;

    if (!ConcatPath(Ptfs, NewFileName, NewFullPath))
        return STATUS_OBJECT_NAME_INVALID;

    if (!MoveFileExW(FullPath, NewFullPath, ReplaceIfExists ? MOVEFILE_REPLACE_EXISTING : 0))
        return FspNtStatusFromWin32(GetLastError());

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS GetSecurity(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext,
    PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize)
{
    StatsIncr(GetSecurity);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoGetSecurity((PTFS_FILE_CONTEXT*)FileContext, SecurityDescriptor, PSecurityDescriptorSize);

#else
    HANDLE Handle = HandleFromContext(FileContext);
    DWORD SecurityDescriptorSizeNeeded = 0;

    if (!GetKernelObjectSecurity(Handle,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        SecurityDescriptor, (DWORD)*PSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
    {
        *PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
        return FspNtStatusFromWin32(GetLastError());
    }

    *PSecurityDescriptorSize = SecurityDescriptorSizeNeeded;

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS SetSecurity(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext,
    SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor)
{
    StatsIncr(SetSecurity);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoSetSecurity((PTFS_FILE_CONTEXT*)FileContext, SecurityInformation, ModificationDescriptor);

#else
    HANDLE Handle = HandleFromContext(FileContext);

    if (!SetKernelObjectSecurity(Handle, SecurityInformation, ModificationDescriptor))
        return FspNtStatusFromWin32(GetLastError());

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext0, PWSTR Pattern, PWSTR Marker,
    PVOID Buffer, ULONG BufferLength, PULONG PBytesTransferred)
{
    StatsIncr(ReadDirectory);

    //PTFS *Ptfs = (PTFS *)FileSystem->UserContext;
    PTFS_FILE_CONTEXT *FileContext = (PTFS_FILE_CONTEXT*)FileContext0;

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoReadDirectory(FileContext, Pattern, Marker, Buffer, BufferLength, PBytesTransferred);

#else
    HANDLE Handle = HandleFromContext(FileContext);
    WCHAR FullPath[FULLPATH_SIZE] = { 0 };
    ULONG Length = 0, PatternLength = 0;
    HANDLE FindHandle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW FindData = { 0 };
    union
    {
        UINT8 B[FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf) + MAX_PATH * sizeof(WCHAR)];
        FSP_FSCTL_DIR_INFO D;
#pragma warning(suppress: 4815)
    } DirInfoBuf = { 0 };
    FSP_FSCTL_DIR_INFO *DirInfo = &DirInfoBuf.D;
    NTSTATUS DirBufferResult;

    DirBufferResult = STATUS_SUCCESS;
    if (FspFileSystemAcquireDirectoryBuffer(&FileContext->DirBuffer, 0 == Marker, &DirBufferResult))
    {
        WCHAR STRING_ASTERISK[] = L"*";
        if (0 == Pattern)
            Pattern = STRING_ASTERISK;
        PatternLength = (ULONG)wcslen(Pattern);

        Length = GetFinalPathNameByHandleW(Handle, FullPath, FULLPATH_SIZE - 1, 0);
        if (0 == Length)
            DirBufferResult = FspNtStatusFromWin32(GetLastError());
        else if (Length + 1 + PatternLength >= FULLPATH_SIZE)
            DirBufferResult = STATUS_OBJECT_NAME_INVALID;
        if (!NT_SUCCESS(DirBufferResult))
        {
            FspFileSystemReleaseDirectoryBuffer(&FileContext->DirBuffer);
            return DirBufferResult;
        }

        if (Length > 0)
        {
            if (L'\\' != FullPath[Length - 1])
                FullPath[Length++] = L'\\';
        }
        memcpy(FullPath + Length, Pattern, PatternLength * sizeof(WCHAR));
        FullPath[Length + PatternLength] = L'\0';

        FindHandle = FindFirstFileW(FullPath, &FindData);
        if (INVALID_HANDLE_VALUE != FindHandle)
        {
            do
            {
                memset(DirInfo, 0, sizeof *DirInfo);
                Length = (ULONG)wcslen(FindData.cFileName);
                DirInfo->Size = (UINT16)(FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf) + Length * sizeof(WCHAR));
                DirInfo->FileInfo.FileAttributes = FindData.dwFileAttributes;
                DirInfo->FileInfo.ReparseTag = 0;
                DirInfo->FileInfo.FileSize =
                    ((UINT64)FindData.nFileSizeHigh << 32) | (UINT64)FindData.nFileSizeLow;
                DirInfo->FileInfo.AllocationSize = (DirInfo->FileInfo.FileSize + ALLOCATION_UNIT - 1)
                    / ALLOCATION_UNIT * ALLOCATION_UNIT;
                DirInfo->FileInfo.CreationTime = ((PLARGE_INTEGER)&FindData.ftCreationTime)->QuadPart;
                DirInfo->FileInfo.LastAccessTime = ((PLARGE_INTEGER)&FindData.ftLastAccessTime)->QuadPart;
                DirInfo->FileInfo.LastWriteTime = ((PLARGE_INTEGER)&FindData.ftLastWriteTime)->QuadPart;
                DirInfo->FileInfo.ChangeTime = DirInfo->FileInfo.LastWriteTime;
                DirInfo->FileInfo.IndexNumber = 0;
                DirInfo->FileInfo.HardLinks = 0;
                memcpy(DirInfo->FileNameBuf, FindData.cFileName, Length * sizeof(WCHAR));

                if (!FspFileSystemFillDirectoryBuffer(&FileContext->DirBuffer, DirInfo, &DirBufferResult))
                    break;
            } while (FindNextFileW(FindHandle, &FindData));

            FindClose(FindHandle);
        }

        FspFileSystemReleaseDirectoryBuffer(&FileContext->DirBuffer);
    }

    if (!NT_SUCCESS(DirBufferResult))
        return DirBufferResult;

    FspFileSystemReadDirectoryBuffer(&FileContext->DirBuffer,
        Marker, Buffer, BufferLength, PBytesTransferred);

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS SetDelete(FSP_FILE_SYSTEM *FileSystem,
    PVOID FileContext, PWSTR FileName, BOOLEAN DeleteFile)
{
    StatsIncr(SetDelete);

#if !WINFSP_PASSTHROUGH
    return CSDriver()->DoSetDelete((PTFS_FILE_CONTEXT*)FileContext, FileName, DeleteFile);

    /*
    https://stackoverflow.com/questions/36217150/deleting-a-file-based-on-disk-id

    SetFileInformationByHandle を FILE_DISPOSITION_INFO と共に使用すると、
    開いているハンドルを持つファイルを、すべてのハンドルが閉じられたときに削除されるように設定できます。
    */
#else
    HANDLE Handle = HandleFromContext(FileContext);
    FILE_DISPOSITION_INFO DispositionInfo = { 0 };

    DispositionInfo.DeleteFile = DeleteFile;

    if (!SetFileInformationByHandle(Handle,
        FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
        return FspNtStatusFromWin32(GetLastError());

    return STATUS_SUCCESS;
#endif
}

/*
static FSP_FILE_SYSTEM_INTERFACE PtfsInterface =
{
    .GetVolumeInfo = GetVolumeInfo,
    .SetVolumeLabel = SetVolumeLabel_,
    .GetSecurityByName = GetSecurityByName,
    .Create = Create,
    .Open = Open,
    .Overwrite = Overwrite,
    .Cleanup = Cleanup,
    .Close = Close,
    .Read = Read,
    .Write = Write,
    .Flush = Flush,
    .GetFileInfo = GetFileInfo,
    .SetBasicInfo = SetBasicInfo,
    .SetFileSize = SetFileSize,
    .Rename = Rename,
    .GetSecurity = GetSecurity,
    .SetSecurity = SetSecurity,
    .ReadDirectory = ReadDirectory,
    .SetDelete = SetDelete,
};
*/

static VOID PtfsDelete(PTFS *Ptfs);

static NTSTATUS PtfsCreate(PWSTR Path, PWSTR VolumePrefix, PWSTR MountPoint, UINT32 DebugFlags,
#if !WINFSP_PASSTHROUGH
    FSP_FSCTL_VOLUME_PARAMS* VolumeParams,
#endif
    PTFS **PPtfs)
{
    WCHAR FullPath[MAX_PATH] = { 0 };
    ULONG Length = 0;
    HANDLE Handle = INVALID_HANDLE_VALUE;
    FILETIME CreationTime = { 0 };
    DWORD LastError = ERROR_SUCCESS;
#if WINFSP_PASSTHROUGH
    FSP_FSCTL_VOLUME_PARAMS VolumeParams = { 0 };
#endif
    PTFS *Ptfs = 0;
    NTSTATUS Result = STATUS_UNSUCCESSFUL;

    *PPtfs = 0;

    Handle = CreateFileW(
        Path, FILE_READ_ATTRIBUTES, 0, 0,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (INVALID_HANDLE_VALUE == Handle)
        return FspNtStatusFromWin32(GetLastError());

    Length = GetFinalPathNameByHandleW(Handle, FullPath, MAX_PATH - 1, 0);
    if (0 == Length)
    {
        LastError = GetLastError();
        CloseHandle(Handle);
        return FspNtStatusFromWin32(LastError);
    }
    if (L'\\' == FullPath[Length - 1])
        FullPath[--Length] = L'\0';

    if (!GetFileTime(Handle, &CreationTime, 0, 0))
    {
        LastError = GetLastError();
        CloseHandle(Handle);
        return FspNtStatusFromWin32(LastError);
    }

    CloseHandle(Handle);

    /* from now on we must goto exit on failure */

    Ptfs = (PTFS*)malloc(sizeof *Ptfs);
    if (0 == Ptfs)
    {
        Result = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }
    memset(Ptfs, 0, sizeof *Ptfs);

    Length = (((ULONGLONG)Length) + 1) * sizeof(WCHAR);
    //Length = (static_cast<unsigned long long>(Length) + 1) * sizeof(WCHAR);
    Ptfs->Path = (PWSTR)malloc(Length);
    if (0 == Ptfs->Path)
    {
        Result = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    memcpy(Ptfs->Path, FullPath, Length);

#if WINFSP_PASSTHROUGH
    memset(&VolumeParams, 0, sizeof VolumeParams);
    VolumeParams.SectorSize = ALLOCATION_UNIT;
    VolumeParams.SectorsPerAllocationUnit = 1;
    VolumeParams.VolumeCreationTime = ((PLARGE_INTEGER)&CreationTime)->QuadPart;
    VolumeParams.VolumeSerialNumber = 0;
    VolumeParams.FileInfoTimeout = 1000;
    VolumeParams.CaseSensitiveSearch = 0;
    VolumeParams.CasePreservedNames = 1;
    VolumeParams.UnicodeOnDisk = 1;
    VolumeParams.PersistentAcls = 1;
    VolumeParams.PostCleanupWhenModifiedOnly = 1;
    VolumeParams.PassQueryDirectoryPattern = 1;
    VolumeParams.FlushAndPurgeOnCleanup = 1;
    VolumeParams.UmFileContextIsUserContext2 = 1;

    if (0 != VolumePrefix)
        wcscpy_s(VolumeParams.Prefix, sizeof VolumeParams.Prefix / sizeof(WCHAR), VolumePrefix);
    wcscpy_s(VolumeParams.FileSystemName, sizeof VolumeParams.FileSystemName / sizeof(WCHAR),
        PROGNAME);

    {
        WCHAR STRING_NET[] = L"" FSP_FSCTL_NET_DEVICE_NAME;
        WCHAR STRING_DSK[] = L"" FSP_FSCTL_DISK_DEVICE_NAME;
        Result = FspFileSystemCreate(
            VolumeParams.Prefix[0] ? STRING_NET : STRING_DSK,
            &VolumeParams,
            //&PtfsInterface,
            getPtfsInterface(),
            &Ptfs->FileSystem);
    }

#else
    VolumeParams->VolumeCreationTime = ((PLARGE_INTEGER)&CreationTime)->QuadPart;

    if (0 != VolumePrefix)
    {
        wcscpy_s(VolumeParams->Prefix, _countof(VolumeParams->Prefix), VolumePrefix);
        //wcscpy_s(VolumeParams->Prefix, sizeof VolumeParams->Prefix / sizeof(WCHAR), VolumePrefix);
    }

    wcscpy_s(VolumeParams->FileSystemName, _countof(VolumeParams->FileSystemName), PROGNAME);
    //wcscpy_s(VolumeParams->FileSystemName, sizeof VolumeParams->FileSystemName / sizeof(WCHAR), PROGNAME);

    {
        WCHAR STRING_NET[] = L"" FSP_FSCTL_NET_DEVICE_NAME;
        WCHAR STRING_DSK[] = L"" FSP_FSCTL_DISK_DEVICE_NAME;

        Result = FspFileSystemCreate(
            VolumeParams->Prefix[0] ? STRING_NET : STRING_DSK,
            VolumeParams,
            //&PtfsInterface,
            getPtfsInterface(),
            &Ptfs->FileSystem);
    }

#endif

    if (!NT_SUCCESS(Result))
        goto exit;
    Ptfs->FileSystem->UserContext = Ptfs;

    Result = FspFileSystemSetMountPoint(Ptfs->FileSystem, MountPoint);
    if (!NT_SUCCESS(Result))
        goto exit;

    FspFileSystemSetDebugLog(Ptfs->FileSystem, DebugFlags);

    Result = STATUS_SUCCESS;

exit:
    if (NT_SUCCESS(Result))
        *PPtfs = Ptfs;
    else if (0 != Ptfs)
        PtfsDelete(Ptfs);

    return Result;
}

static VOID PtfsDelete(PTFS *Ptfs)
{
    if (0 != Ptfs->FileSystem)
        FspFileSystemDelete(Ptfs->FileSystem);

    if (0 != Ptfs->Path)
        free(Ptfs->Path);

    free(Ptfs);
}

static NTSTATUS EnableBackupRestorePrivileges(VOID)
{
    union
    {
        TOKEN_PRIVILEGES P;
        UINT8 B[sizeof(TOKEN_PRIVILEGES) + sizeof(LUID_AND_ATTRIBUTES)];
    } Privileges = { 0 };
    HANDLE Token = INVALID_HANDLE_VALUE;

    Privileges.P.PrivilegeCount = 2;
    Privileges.P.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Privileges.P.Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValueW(0, SE_BACKUP_NAME, &Privileges.P.Privileges[0].Luid) ||
        !LookupPrivilegeValueW(0, SE_RESTORE_NAME, &Privileges.P.Privileges[1].Luid))
        return FspNtStatusFromWin32(GetLastError());

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
        return FspNtStatusFromWin32(GetLastError());

    if (!AdjustTokenPrivileges(Token, FALSE, &Privileges.P, 0, 0, 0))
    {
        CloseHandle(Token);

        return FspNtStatusFromWin32(GetLastError());
    }

    CloseHandle(Token);

    return STATUS_SUCCESS;
}

static ULONG wcstol_deflt(wchar_t *w, ULONG deflt)
{
    wchar_t *endp = nullptr;
    ULONG ul = wcstol(w, &endp, 0);
    return L'\0' != w[0] && L'\0' == *endp ? ul : deflt;
}

static NTSTATUS SvcStart(FSP_SERVICE *Service, ULONG argc, PWSTR *argv)
{
#define argtos(v)                       if (arge > ++argp) v = *argp; else goto usage
#define argtol(v)                       if (arge > ++argp) v = wcstol_deflt(*argp, v); else goto usage

    wchar_t **argp = nullptr, **arge = nullptr;
    PWSTR DebugLogFile = 0;
    ULONG DebugFlags = 0;
    PWSTR VolumePrefix = 0;
    PWSTR PassThrough = 0;
    PWSTR MountPoint = 0;
    HANDLE DebugLogHandle = INVALID_HANDLE_VALUE;
    WCHAR PassThroughBuf[MAX_PATH] = { 0 };
    PTFS *Ptfs = 0;
    NTSTATUS Result = STATUS_UNSUCCESSFUL;
#if !WINFSP_PASSTHROUGH
    FSP_FSCTL_VOLUME_PARAMS VolumeParams = { 0 };

    VolumeParams.SectorSize = ALLOCATION_UNIT;
    VolumeParams.SectorsPerAllocationUnit = 1;
    VolumeParams.VolumeSerialNumber = 0;
    VolumeParams.FileInfoTimeout = 1000;
    VolumeParams.CaseSensitiveSearch = 0;
    VolumeParams.CasePreservedNames = 1;
    VolumeParams.UnicodeOnDisk = 1;
    VolumeParams.PersistentAcls = 1;
    VolumeParams.PostCleanupWhenModifiedOnly = 1;
    VolumeParams.PassQueryDirectoryPattern = 1;
    VolumeParams.FlushAndPurgeOnCleanup = 1;
    VolumeParams.UmFileContextIsUserContext2 = 1;
#endif

    for (argp = argv + 1, arge = argv + argc; arge > argp; argp++)
    {
        if (L'-' != argp[0][0])
            break;
        switch (argp[0][1])
        {
        case L'?':
            goto usage;
        case L'd':
            argtol(DebugFlags);
            break;
        case L'D':
            argtos(DebugLogFile);
            break;
        case L'm':
            argtos(MountPoint);
            break;
        case L'p':
            argtos(PassThrough);
            break;
        case L'u':
            argtos(VolumePrefix);
            break;
//#if !WINFSP_PASSTHROUGH
        case L'S':
        case L'T':
        {
            PWSTR ign = 0;
            argtos(ign);
            break;
        }
//#endif
        default:
            goto usage;
        }
    }

    if (arge > argp)
        goto usage;

    if (0 == PassThrough && 0 != VolumePrefix)
    {
        PWSTR P;

        P = wcschr(VolumePrefix, L'\\');
        if (0 != P && L'\\' != P[1])
        {
            P = wcschr(P + 1, L'\\');
            if (0 != P &&
                (
                (L'A' <= P[1] && P[1] <= L'Z') ||
                (L'a' <= P[1] && P[1] <= L'z')
                ) &&
                L'$' == P[2])
            {
                StringCbPrintf(PassThroughBuf, sizeof PassThroughBuf, L"%c:%s", P[1], P + 3);
                PassThrough = PassThroughBuf;
            }
        }
    }

    if (0 == PassThrough || 0 == MountPoint)
        goto usage;

    EnableBackupRestorePrivileges();

    if (0 != DebugLogFile)
    {
        if (0 == wcscmp(L"-", DebugLogFile))
            DebugLogHandle = GetStdHandle(STD_ERROR_HANDLE);
        else
            DebugLogHandle = CreateFileW(
                DebugLogFile,
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                0,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                0);
        if (INVALID_HANDLE_VALUE == DebugLogHandle)
        {
            fail(L"cannot open debug log file");
            goto usage;
        }

        FspDebugLogSetHandle(DebugLogHandle);
    }

#if !WINFSP_PASSTHROUGH
    if (!CSDriver()->PreCreateFilesystem(Service, PassThrough, &VolumeParams))
    {
        fail(L"fault: PreCreateFilesystem");

        Result = STATUS_APP_INIT_FAILURE;
        goto exit;
    }
#endif

    Result = PtfsCreate(PassThrough, VolumePrefix, MountPoint, DebugFlags,
#if !WINFSP_PASSTHROUGH
        &VolumeParams,
#endif
        &Ptfs);
    if (!NT_SUCCESS(Result))
    {
        fail(L"cannot create file system");
        goto exit;
    }

#if !WINFSP_PASSTHROUGH
    if (!CSDriver()->OnSvcStart(PassThrough, Ptfs->FileSystem))
    {
        fail(L"fault: OnSvcStart");

        Result = STATUS_APP_INIT_FAILURE;
        goto exit;
    }
#endif

    Result = FspFileSystemStartDispatcher(Ptfs->FileSystem, 0);
    if (!NT_SUCCESS(Result))
    {
        fail(L"cannot start file system");
        goto exit;
    }

    MountPoint = FspFileSystemMountPoint(Ptfs->FileSystem);

    info(L"%s%s%s -p %s -m %s",
        PROGNAME,
        0 != VolumePrefix && L'\0' != VolumePrefix[0] ? L" -u " : L"",
            0 != VolumePrefix && L'\0' != VolumePrefix[0] ? VolumePrefix : L"",
        PassThrough,
        MountPoint);

    Service->UserContext = Ptfs;
    Result = STATUS_SUCCESS;

exit:
    if (!NT_SUCCESS(Result) && 0 != Ptfs)
        PtfsDelete(Ptfs);

    return Result;

usage:
    static wchar_t usage[] = L""
        "usage: %s OPTIONS\n"
        "\n"
        "options:\n"
        "    -d DebugFlags       [-1: enable all debug logs]\n"
        "    -D DebugLogFile     [file path; use - for stderr]\n"
        "    -u \\Server\\Share    [UNC prefix (single backslash)]\n"
        "    -p Directory        [directory to expose as pass through file system]\n"
        "    -m MountPoint       [X:|*|directory]\n"
        "    -T TraceLogDir      [dir path]\n";

    fail(usage, PROGNAME);

    return STATUS_UNSUCCESSFUL;

#undef argtos
#undef argtol
}

static NTSTATUS SvcStop(FSP_SERVICE *Service)
{
    PTFS *Ptfs = (PTFS*)Service->UserContext;

    FspFileSystemStopDispatcher(Ptfs->FileSystem);

#if !WINFSP_PASSTHROUGH
    CSDriver()->OnSvcStop();
#endif

    PtfsDelete(Ptfs);

    return STATUS_SUCCESS;
}

static FSP_FILE_SYSTEM_INTERFACE gPtfsInterface_;

static const FSP_FILE_SYSTEM_INTERFACE* getPtfsInterface()
{
    static bool firstTime = true;

    if (firstTime)
    {
        firstTime = false;

        gPtfsInterface_.GetVolumeInfo = GetVolumeInfo;
        gPtfsInterface_.SetVolumeLabel = SetVolumeLabel_;
        gPtfsInterface_.GetSecurityByName = GetSecurityByName;
        gPtfsInterface_.Create = Create;
        gPtfsInterface_.Open = Open;
        gPtfsInterface_.Overwrite = Overwrite;
        gPtfsInterface_.Cleanup = Cleanup;
        gPtfsInterface_.Close = Close;
        gPtfsInterface_.Read = Read;
        gPtfsInterface_.Write = Write;
        gPtfsInterface_.Flush = Flush;
        gPtfsInterface_.GetFileInfo = GetFileInfo;
        gPtfsInterface_.SetBasicInfo = SetBasicInfo;
        gPtfsInterface_.SetFileSize = SetFileSize;
        gPtfsInterface_.Rename = Rename;
        gPtfsInterface_.GetSecurity = GetSecurity;
        gPtfsInterface_.SetSecurity = SetSecurity;
        gPtfsInterface_.ReadDirectory = ReadDirectory;
        gPtfsInterface_.SetDelete = SetDelete;
    }

    return &gPtfsInterface_;
}

//int wmain(int argc, wchar_t **argv)
int WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WINFSP_IF* argWinFspIf)
{
    gWinFspIf = argWinFspIf;
    PROGNAME = progname;

    if (!NT_SUCCESS(FspLoad(0)))
        return ERROR_DELAY_LOAD_FAILED;

    return FspServiceRun(PROGNAME, SvcStart, SvcStop, 0);
}
