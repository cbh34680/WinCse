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

#include "WinCseLib_c.h"
#if !WINFSP_PASSTHROUGH
#include <iostream>
#endif

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
    FSP_FILE_SYSTEM* FileSystem;
    PWSTR Path;
} PTFS;

typedef struct
{
    HANDLE Handle;
    PVOID DirBuffer;
} PTFS_FILE_CONTEXT;


WINFSP_STATS* gFspStats;
#define StatsIncr(fname)    if (gFspStats) InterlockedIncrement(& (gFspStats->fname))

static const FSP_FILE_SYSTEM_INTERFACE* getPtfsInterface();

/*static*/ NTSTATUS GetFileInfoInternal(HANDLE Handle, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    StatsIncr(GetFileInfoInternal);

#if !WINFSP_PASSTHROUGH
    _ASSERT(pFileInfo);
#endif

    BY_HANDLE_FILE_INFORMATION ByHandleFileInfo = { 0 };

    if (!GetFileInformationByHandle(Handle, &ByHandleFileInfo))
        return FspNtStatusFromWin32(GetLastError());

    pFileInfo->FileAttributes = ByHandleFileInfo.dwFileAttributes;
    pFileInfo->ReparseTag = 0;
    pFileInfo->FileSize =
        ((UINT64)ByHandleFileInfo.nFileSizeHigh << 32) | (UINT64)ByHandleFileInfo.nFileSizeLow;
    pFileInfo->AllocationSize = (pFileInfo->FileSize + ALLOCATION_UNIT - 1)
        / ALLOCATION_UNIT * ALLOCATION_UNIT;
    pFileInfo->CreationTime = ((PLARGE_INTEGER)&ByHandleFileInfo.ftCreationTime)->QuadPart;
    pFileInfo->LastAccessTime = ((PLARGE_INTEGER)&ByHandleFileInfo.ftLastAccessTime)->QuadPart;
    pFileInfo->LastWriteTime = ((PLARGE_INTEGER)&ByHandleFileInfo.ftLastWriteTime)->QuadPart;
    pFileInfo->ChangeTime = pFileInfo->LastWriteTime;
    pFileInfo->IndexNumber =
        ((UINT64)ByHandleFileInfo.nFileIndexHigh << 32) | (UINT64)ByHandleFileInfo.nFileIndexLow;
    pFileInfo->HardLinks = 0;

    return STATUS_SUCCESS;
}

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo)
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

static NTSTATUS SetVolumeLabel_(FSP_FILE_SYSTEM* FileSystem, PWSTR VolumeLabel,
    FSP_FSCTL_VOLUME_INFO *VolumeInfo)
{
    StatsIncr(SetVolumeLabel_);

    /* we do not support changing the volume label */
    return STATUS_INVALID_DEVICE_REQUEST;
}

#if !WINFSP_PASSTHROUGH

NTSTATUS (CSELIB::ICSDriver::*RelayPreCreateFilesystem)(FSP_SERVICE* Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* argVolumeParams);
NTSTATUS (CSELIB::ICSDriver::*RelayOnSvcStart)(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem);
VOID     (CSELIB::ICSDriver::*RelayOnSvcStop)();
NTSTATUS (CSELIB::ICSDriver::*RelayGetSecurityByName)(PCWSTR argFileName, PUINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize);
NTSTATUS (CSELIB::ICSDriver::*RelayCreate)(PCWSTR argFileName, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, CSELIB::IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo);
NTSTATUS (CSELIB::ICSDriver::*RelayOpen)(PCWSTR argFileName, UINT32 argCreateOptions, UINT32 argGrantedAccess, CSELIB::IFileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo);
NTSTATUS (CSELIB::ICSDriver::*RelayOverwrite)(CSELIB::IFileContext* argFileContext, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo);
VOID     (CSELIB::ICSDriver::*RelayCleanup)(CSELIB::IFileContext* argFileContext, PWSTR argFileName, ULONG argFlags);
VOID     (CSELIB::ICSDriver::*RelayClose)(CSELIB::IFileContext* argFileContext);
NTSTATUS (CSELIB::ICSDriver::*RelayRead)(CSELIB::IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred);
NTSTATUS (CSELIB::ICSDriver::*RelayWrite)(CSELIB::IFileContext* argFileContext, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* argFileInfo);
NTSTATUS (CSELIB::ICSDriver::*RelayFlush)(CSELIB::IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo);
NTSTATUS (CSELIB::ICSDriver::*RelayGetFileInfo)(CSELIB::IFileContext* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo);
NTSTATUS (CSELIB::ICSDriver::*RelaySetBasicInfo)(CSELIB::IFileContext* argFileContext, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* argFileInfo);
NTSTATUS (CSELIB::ICSDriver::*RelaySetFileSize)(CSELIB::IFileContext* argFileContext, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo);
NTSTATUS (CSELIB::ICSDriver::*RelayRename)(CSELIB::IFileContext* argFileContext, PWSTR argFileName,PWSTR argNewFileName, BOOLEAN argReplaceIfExists);
NTSTATUS (CSELIB::ICSDriver::*RelayGetSecurity)(CSELIB::IFileContext* argFileContext, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize);
NTSTATUS (CSELIB::ICSDriver::*RelaySetSecurity)(CSELIB::IFileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor);
NTSTATUS (CSELIB::ICSDriver::*RelayReadDirectory)(CSELIB::IFileContext* argFileContext, PWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred);
NTSTATUS (CSELIB::ICSDriver::*RelaySetDelete)(CSELIB::IFileContext* argFileContext, PWSTR argFileName, BOOLEAN argDeleteFile);

CSELIB::ICSDriver* gCSDriver;

#define SET_METHOD_ADDR(name)   name = &CSELIB::ICSDriver::name

void setupWinCseGlobal(WINCSE_IF* argWinCseIf)
{
    //gWinCseIf = argWinCseIf;
    gFspStats = &argWinCseIf->FspStats;
    gCSDriver = argWinCseIf->mDriver;

    SET_METHOD_ADDR(RelayPreCreateFilesystem);
    SET_METHOD_ADDR(RelayOnSvcStart);
    SET_METHOD_ADDR(RelayOnSvcStop);
    SET_METHOD_ADDR(RelayGetSecurityByName);
    SET_METHOD_ADDR(RelayCreate);
    SET_METHOD_ADDR(RelayOpen);
    SET_METHOD_ADDR(RelayOverwrite);
    SET_METHOD_ADDR(RelayCleanup);
    SET_METHOD_ADDR(RelayClose);
    SET_METHOD_ADDR(RelayRead);
    SET_METHOD_ADDR(RelayWrite);
    SET_METHOD_ADDR(RelayFlush);
    SET_METHOD_ADDR(RelayGetFileInfo);
    SET_METHOD_ADDR(RelaySetBasicInfo);
    SET_METHOD_ADDR(RelaySetFileSize);
    SET_METHOD_ADDR(RelayRename);
    SET_METHOD_ADDR(RelayGetSecurity);
    SET_METHOD_ADDR(RelaySetSecurity);
    SET_METHOD_ADDR(RelayReadDirectory);
    SET_METHOD_ADDR(RelaySetDelete);
}

template<typename MethodT, typename... Args>
NTSTATUS relayReturnable(const MethodT method, Args... args)
{
    try
    {
        return (gCSDriver->*method)(args...);
    }
    catch (const CSELIB::FatalError& e)
    {
        std::cerr << e.what() << std::endl;
        return e.mNtstatus;
    }
#ifdef _RELEASE
    catch (...)
    {
        std::cerr << "unknown error" << std::endl;
        return STATUS_UNSUCCESSFUL;
    }
#endif
}

template<typename MethodT, typename... Args>
VOID relayNonReturnable(const MethodT method, Args... args)
{
    try
    {
        (gCSDriver->*method)(args...);
    }
    catch (const CSELIB::FatalError& e)
    {
        std::cerr << e.what() << std::endl;
    }
#ifdef _RELEASE
    catch (...)
    {
        std::cerr << "unknown error" << std::endl;
    }
#endif
}

#endif      // !WINFSP_PASSTHROUGH

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* FileSystem, PWSTR argFileName,
    PUINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize)
{
    StatsIncr(GetSecurityByName);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayGetSecurityByName, argFileName, argFileAttributes, argSecurityDescriptor, argSecurityDescriptorSize);

#else
    PTFS* Ptfs = (PTFS*)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 };
    HANDLE Handle = INVALID_HANDLE_VALUE;
    FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo = { 0 };
    DWORD SecurityDescriptorSizeNeeded = 0;
    NTSTATUS Result = STATUS_UNSUCCESSFUL;

    if (!ConcatPath(Ptfs, argFileName, FullPath))
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

    if (0 != argFileAttributes)
    {
        if (!GetFileInformationByHandleEx(Handle,
            FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
        {
            Result = FspNtStatusFromWin32(GetLastError());
            goto exit;
        }

        *argFileAttributes = AttributeTagInfo.FileAttributes;
    }

    if (0 != argSecurityDescriptorSize)
    {
        if (!GetKernelObjectSecurity(Handle,
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            argSecurityDescriptor, (DWORD)*argSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
        {
            *argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
            Result = FspNtStatusFromWin32(GetLastError());
            goto exit;
        }

        *argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
    }

    Result = STATUS_SUCCESS;

exit:
    if (INVALID_HANDLE_VALUE != Handle)
        CloseHandle(Handle);

    return Result;
#endif
}

static NTSTATUS Create(FSP_FILE_SYSTEM* FileSystem, PWSTR argFileName, UINT32 argCreateOptions,
    UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor,
    UINT64 argAllocationSize, PVOID* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(Create);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayCreate, argFileName, argCreateOptions, argGrantedAccess, argFileAttributes, argSecurityDescriptor, argAllocationSize, (CSELIB::IFileContext**)argFileContext, argFileInfo);

#else
    PTFS* Ptfs = (PTFS*)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 };
    SECURITY_ATTRIBUTES SecurityAttributes = { 0 };
    ULONG CreateFlags = 0;
    PTFS_FILE_CONTEXT* PtfsFileContext = nullptr;

    if (!ConcatPath(Ptfs, argFileName, FullPath))
        return STATUS_OBJECT_NAME_INVALID;

    PtfsFileContext = (PTFS_FILE_CONTEXT*)malloc(sizeof *PtfsFileContext);
    if (0 == PtfsFileContext)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(PtfsFileContext, 0, sizeof *PtfsFileContext);

    SecurityAttributes.nLength = sizeof SecurityAttributes;
    SecurityAttributes.lpSecurityDescriptor = argSecurityDescriptor;
    SecurityAttributes.bInheritHandle = FALSE;

    CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
    if (argCreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    if (argCreateOptions & FILE_DIRECTORY_FILE)
    {
        /*
         * It is not widely known but CreateFileW can be used to create directories!
         * It requires the specification of both FILE_FLAG_BACKUP_SEMANTICS and
         * FILE_FLAG_POSIX_SEMANTICS. It also requires that FileAttributes has
         * FILE_ATTRIBUTE_DIRECTORY set.
         */
        CreateFlags |= FILE_FLAG_POSIX_SEMANTICS;
        argFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    }
    else
        argFileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

    if (0 == argFileAttributes)
        argFileAttributes = FILE_ATTRIBUTE_NORMAL;

    PtfsFileContext->Handle = CreateFileW(FullPath,
        argGrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &SecurityAttributes,
        CREATE_NEW, CreateFlags | argFileAttributes, 0);
    if (INVALID_HANDLE_VALUE == PtfsFileContext->Handle)
    {
        free(PtfsFileContext);

        DWORD lerr = GetLastError();
        if (lerr == ERROR_FILE_EXISTS)
        {
            // https://github.com/winfsp/winfsp/issues/601
            return STATUS_OBJECT_NAME_COLLISION;
        }
        return FspNtStatusFromWin32(lerr);
    }

    *argFileContext = PtfsFileContext;

    return GetFileInfoInternal(PtfsFileContext->Handle, argFileInfo);
#endif
}

static NTSTATUS Open(FSP_FILE_SYSTEM* FileSystem, PWSTR argFileName, UINT32 argCreateOptions,
    UINT32 argGrantedAccess, PVOID* argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(Open);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayOpen, argFileName, argCreateOptions, argGrantedAccess, (CSELIB::IFileContext**)argFileContext, argFileInfo);

#else
    PTFS *Ptfs = (PTFS *)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 };
    ULONG CreateFlags = 0;
    PTFS_FILE_CONTEXT *PtfsFileContext = nullptr;

    if (!ConcatPath(Ptfs, argFileName, FullPath))
        return STATUS_OBJECT_NAME_INVALID;

    PtfsFileContext = (PTFS_FILE_CONTEXT*)malloc(sizeof *PtfsFileContext);
    if (0 == PtfsFileContext)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(PtfsFileContext, 0, sizeof *PtfsFileContext);

    CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
    if (argCreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    PtfsFileContext->Handle = CreateFileW(FullPath,
        argGrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        OPEN_EXISTING, CreateFlags, 0);
    if (INVALID_HANDLE_VALUE == PtfsFileContext->Handle)
    {
        free(PtfsFileContext);
        return FspNtStatusFromWin32(GetLastError());
    }

    *argFileContext = PtfsFileContext;

    return GetFileInfoInternal(PtfsFileContext->Handle, argFileInfo);
#endif
}

static NTSTATUS Overwrite(FSP_FILE_SYSTEM* FileSystem,
    PVOID argFileContext, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize,
    FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(Overwrite);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayOverwrite, (CSELIB::IFileContext*)argFileContext, argFileAttributes, argReplaceFileAttributes, argAllocationSize, argFileInfo);

#else
    HANDLE Handle = HandleFromContext(argFileContext);
    FILE_BASIC_INFO BasicInfo = { 0 };
    FILE_ALLOCATION_INFO AllocationInfo = { 0 };
    FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo = { 0 };

    if (argReplaceFileAttributes)
    {
        if (0 == argFileAttributes)
            argFileAttributes = FILE_ATTRIBUTE_NORMAL;

        BasicInfo.FileAttributes = argFileAttributes;
        if (!SetFileInformationByHandle(Handle,
            FileBasicInfo, &BasicInfo, sizeof BasicInfo))
            return FspNtStatusFromWin32(GetLastError());
    }
    else if (0 != argFileAttributes)
    {
        if (!GetFileInformationByHandleEx(Handle,
            FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
            return FspNtStatusFromWin32(GetLastError());

        BasicInfo.FileAttributes = argFileAttributes | AttributeTagInfo.FileAttributes;
        if (BasicInfo.FileAttributes ^ argFileAttributes)
        {
            if (!SetFileInformationByHandle(Handle,
                FileBasicInfo, &BasicInfo, sizeof BasicInfo))
                return FspNtStatusFromWin32(GetLastError());
        }
    }

    if (!SetFileInformationByHandle(Handle,
        FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, argFileInfo);
#endif
}

static VOID Cleanup(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext, PWSTR argFileName, ULONG argFlags)
{
    StatsIncr(Cleanup);

#if !WINFSP_PASSTHROUGH
    relayNonReturnable(RelayCleanup, (CSELIB::IFileContext*)argFileContext, argFileName, argFlags);

#else
    HANDLE Handle = HandleFromContext(argFileContext);

    if (argFlags & FspCleanupDelete)
    {
        CloseHandle(Handle);

        /* this will make all future uses of Handle to fail with STATUS_INVALID_HANDLE */
        HandleFromContext(argFileContext) = INVALID_HANDLE_VALUE;
    }
#endif
}

static VOID Close(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext)
{
    StatsIncr(Close);

#if !WINFSP_PASSTHROUGH
    relayNonReturnable(RelayClose, (CSELIB::IFileContext*)argFileContext);

#else
    PTFS_FILE_CONTEXT *FileContext = (PTFS_FILE_CONTEXT*)argFileContext;
    HANDLE Handle = HandleFromContext(FileContext);

    CloseHandle(Handle);

    FspFileSystemDeleteDirectoryBuffer(&FileContext->DirBuffer);
    free(FileContext);
#endif
}

static NTSTATUS Read(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext, PVOID argBuffer,
    UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred)
{
    StatsIncr(Read);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayRead, (CSELIB::IFileContext*)argFileContext, argBuffer, argOffset, argLength, argBytesTransferred);

#else
    HANDLE Handle = HandleFromContext(argFileContext);
    OVERLAPPED Overlapped = { 0 };

    Overlapped.Offset = (DWORD)argOffset;
    Overlapped.OffsetHigh = (DWORD)(argOffset >> 32);

    if (!ReadFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
    {
        return FspNtStatusFromWin32(GetLastError());
    }

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS Write(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext, PVOID argBuffer,
    UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo,
    PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(Write);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayWrite, (CSELIB::IFileContext*)argFileContext, argBuffer, argOffset, argLength, argWriteToEndOfFile, argConstrainedIo, argBytesTransferred, argFileInfo);

#else
    HANDLE Handle = HandleFromContext(argFileContext);
    LARGE_INTEGER FileSize = { 0 };
    OVERLAPPED Overlapped = { 0 };

    if (argConstrainedIo)
    {
        if (!GetFileSizeEx(Handle, &FileSize))
            return FspNtStatusFromWin32(GetLastError());

        if (argOffset >= (UINT64)FileSize.QuadPart)
            return STATUS_SUCCESS;
        if (argOffset + argLength > (UINT64)FileSize.QuadPart)
            argLength = (ULONG)((UINT64)FileSize.QuadPart - argOffset);
    }

    Overlapped.Offset = (DWORD)argOffset;
    Overlapped.OffsetHigh = (DWORD)(argOffset >> 32);

    if (!WriteFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, argFileInfo);
#endif
}

static NTSTATUS Flush(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(Flush);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayFlush, (CSELIB::IFileContext*)argFileContext, argFileInfo);

#else
    HANDLE Handle = HandleFromContext(argFileContext);

    /* we do not flush the whole volume, so just return SUCCESS */
    if (0 == Handle)
        return STATUS_SUCCESS;

    if (!FlushFileBuffers(Handle))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, argFileInfo);
#endif
}

static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(GetFileInfo);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayGetFileInfo, (CSELIB::IFileContext*)argFileContext, argFileInfo);

#else
    HANDLE Handle = HandleFromContext(argFileContext);

    return GetFileInfoInternal(Handle, argFileInfo);
#endif
}

static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext, UINT32 argFileAttributes,
    UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime,
    FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(SetBasicInfo);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelaySetBasicInfo, (CSELIB::IFileContext*)argFileContext, argFileAttributes, argCreationTime, argLastAccessTime, argLastWriteTime, argChangeTime, argFileInfo);

#else
    HANDLE Handle = HandleFromContext(argFileContext);

    FILE_BASIC_INFO BasicInfo = { 0 };

    if (INVALID_FILE_ATTRIBUTES == argFileAttributes)
        argFileAttributes = 0;
    else if (0 == argFileAttributes)
        argFileAttributes = FILE_ATTRIBUTE_NORMAL;

    BasicInfo.FileAttributes = argFileAttributes;
    BasicInfo.CreationTime.QuadPart = argCreationTime;
    BasicInfo.LastAccessTime.QuadPart = argLastAccessTime;
    BasicInfo.LastWriteTime.QuadPart = argLastWriteTime;
    //BasicInfo.ChangeTime = argChangeTime;

    if (!SetFileInformationByHandle(Handle,
        FileBasicInfo, &BasicInfo, sizeof BasicInfo))
        return FspNtStatusFromWin32(GetLastError());

    return GetFileInfoInternal(Handle, argFileInfo);
#endif
}

static NTSTATUS SetFileSize(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext,
    UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo)
{
    StatsIncr(SetFileSize);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelaySetFileSize, (CSELIB::IFileContext*)argFileContext, argNewSize, argSetAllocationSize, argFileInfo);

#else
    HANDLE Handle = HandleFromContext(argFileContext);
    FILE_ALLOCATION_INFO AllocationInfo = { 0 };
    FILE_END_OF_FILE_INFO EndOfFileInfo = { 0 };

    if (argSetAllocationSize)
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

        AllocationInfo.AllocationSize.QuadPart = argNewSize;

        if (!SetFileInformationByHandle(Handle,
            FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
            return FspNtStatusFromWin32(GetLastError());
    }
    else
    {
        EndOfFileInfo.EndOfFile.QuadPart = argNewSize;

        if (!SetFileInformationByHandle(Handle,
            FileEndOfFileInfo, &EndOfFileInfo, sizeof EndOfFileInfo))
            return FspNtStatusFromWin32(GetLastError());
    }

    return GetFileInfoInternal(Handle, argFileInfo);
#endif
}

static NTSTATUS Rename(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext,
    PWSTR argFileName, PWSTR argNewFileName, BOOLEAN argReplaceIfExists)
{
    StatsIncr(Rename);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayRename, (CSELIB::IFileContext*)argFileContext, argFileName, argNewFileName, argReplaceIfExists);

#else
    PTFS* Ptfs = (PTFS*)FileSystem->UserContext;
    WCHAR FullPath[FULLPATH_SIZE] = { 0 }, NewFullPath[FULLPATH_SIZE] = { 0 };

    if (!ConcatPath(Ptfs, argFileName, FullPath))
        return STATUS_OBJECT_NAME_INVALID;

    if (!ConcatPath(Ptfs, argNewFileName, NewFullPath))
        return STATUS_OBJECT_NAME_INVALID;

    if (!MoveFileExW(FullPath, NewFullPath, argReplaceIfExists ? MOVEFILE_REPLACE_EXISTING : 0))
        return FspNtStatusFromWin32(GetLastError());

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS GetSecurity(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext,
    PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize)
{
    StatsIncr(GetSecurity);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayGetSecurity, (CSELIB::IFileContext*)argFileContext, argSecurityDescriptor, argSecurityDescriptorSize);

#else
    HANDLE Handle = HandleFromContext(argFileContext);
    DWORD SecurityDescriptorSizeNeeded = 0;

    if (!GetKernelObjectSecurity(Handle,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        argSecurityDescriptor, (DWORD)*argSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
    {
        *argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
        return FspNtStatusFromWin32(GetLastError());
    }

    *argSecurityDescriptorSize = SecurityDescriptorSizeNeeded;

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS SetSecurity(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext,
    SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor)
{
    StatsIncr(SetSecurity);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelaySetSecurity, (CSELIB::IFileContext*)argFileContext, argSecurityInformation, argModificationDescriptor);

#else
    HANDLE Handle = HandleFromContext(argFileContext);

    if (!SetKernelObjectSecurity(Handle, argSecurityInformation, argModificationDescriptor))
        return FspNtStatusFromWin32(GetLastError());

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext,
    PWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred)
{
    StatsIncr(ReadDirectory);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelayReadDirectory, (CSELIB::IFileContext*)argFileContext, argPattern, argMarker, argBuffer, argBufferLength, argBytesTransferred);

#else
    //PTFS *Ptfs = (PTFS *)FileSystem->UserContext;
    PTFS_FILE_CONTEXT *PtfsFileContext = (PTFS_FILE_CONTEXT*)argFileContext;

    HANDLE Handle = HandleFromContext(PtfsFileContext);
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
    if (FspFileSystemAcquireDirectoryBuffer(&PtfsFileContext->DirBuffer, 0 == argMarker, &DirBufferResult))
    {
        WCHAR STRING_ASTERISK[] = L"*";
        if (0 == argPattern)
            argPattern = STRING_ASTERISK;
        PatternLength = (ULONG)wcslen(argPattern);

        Length = GetFinalPathNameByHandleW(Handle, FullPath, FULLPATH_SIZE - 1, 0);
        if (0 == Length)
            DirBufferResult = FspNtStatusFromWin32(GetLastError());
        else if (Length + 1 + PatternLength >= FULLPATH_SIZE)
            DirBufferResult = STATUS_OBJECT_NAME_INVALID;
        if (!NT_SUCCESS(DirBufferResult))
        {
            FspFileSystemReleaseDirectoryBuffer(&PtfsFileContext->DirBuffer);
            return DirBufferResult;
        }

        if (Length > 0)
        {
            if (L'\\' != FullPath[Length - 1])
                FullPath[Length++] = L'\\';
        }
        memcpy(FullPath + Length, argPattern, PatternLength * sizeof(WCHAR));
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

                if (!FspFileSystemFillDirectoryBuffer(&PtfsFileContext->DirBuffer, DirInfo, &DirBufferResult))
                    break;
            } while (FindNextFileW(FindHandle, &FindData));

            FindClose(FindHandle);
        }

        FspFileSystemReleaseDirectoryBuffer(&PtfsFileContext->DirBuffer);
    }

    if (!NT_SUCCESS(DirBufferResult))
        return DirBufferResult;

    FspFileSystemReadDirectoryBuffer(&PtfsFileContext->DirBuffer,
        argMarker, argBuffer, argBufferLength, argBytesTransferred);

    return STATUS_SUCCESS;
#endif
}

static NTSTATUS SetDelete(FSP_FILE_SYSTEM* FileSystem, PVOID argFileContext, PWSTR argFileName, BOOLEAN argDeleteFile)
{
    StatsIncr(SetDelete);

#if !WINFSP_PASSTHROUGH
    return relayReturnable(RelaySetDelete, (CSELIB::IFileContext*)argFileContext, argFileName, argDeleteFile);

    /*
    https://stackoverflow.com/questions/36217150/deleting-a-file-based-on-disk-id

    SetFileInformationByHandle を FILE_DISPOSITION_INFO と共に使用すると、
    開いているハンドルを持つファイルを、すべてのハンドルが閉じられたときに削除されるように設定できます。
    */
#else
    HANDLE Handle = HandleFromContext(argFileContext);
    FILE_DISPOSITION_INFO DispositionInfo = { 0 };

    DispositionInfo.DeleteFile = argDeleteFile;

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
        wcscpy_s(VolumeParams->Prefix, sizeof VolumeParams->Prefix / sizeof(WCHAR), VolumePrefix);
    }

    wcscpy_s(VolumeParams->FileSystemName, sizeof VolumeParams->FileSystemName / sizeof(WCHAR), PROGNAME);

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
    StatsIncr(SvcStart);

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
    Result = relayReturnable(RelayPreCreateFilesystem, Service, PassThrough, &VolumeParams);
    if (!NT_SUCCESS(Result))
    {
        fail(L"fault: PreCreateFilesystem");
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
    Result = relayReturnable(RelayOnSvcStart, Ptfs->Path, Ptfs->FileSystem);
    if (!NT_SUCCESS(Result))
    {
        fail(L"fault: OnSvcStart");
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
    StatsIncr(SvcStop);

    PTFS *Ptfs = (PTFS*)Service->UserContext;

    FspFileSystemStopDispatcher(Ptfs->FileSystem);

#if !WINFSP_PASSTHROUGH
    relayNonReturnable(RelayOnSvcStop);
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

int WinFspMain(int argc, wchar_t** argv, WCHAR* progname, WINCSE_IF* argWinCseIf)
{
    PROGNAME = progname;

#if WINFSP_PASSTHROUGH
    gFspStats = &argWinCseIf->FspStats;
#else
    setupWinCseGlobal(argWinCseIf);
#endif

    if (!NT_SUCCESS(FspLoad(0)))
        return ERROR_DELAY_LOAD_FAILED;

    return FspServiceRun(PROGNAME, SvcStart, SvcStop, 0);
}
