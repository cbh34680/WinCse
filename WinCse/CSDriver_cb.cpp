#include "CSDriver.hpp"

using namespace CSELIB;
using namespace CSEDRV;


NTSTATUS CSDriver::GetSecurityByName(const std::filesystem::path& argWinPath, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize)
{
    NEW_LOG_BLOCK();

    // ActiveDirInfo ����擾

    auto dirInfo{ mActiveDirInfo.get(argWinPath) };
    if (!dirInfo)
    {
        if (mDevice->shouldIgnoreFileName(argWinPath))
        {
            // "desktop.ini" �Ȃǂ͖���������

            //traceW(L"ignore pattern");
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        // �t�@�C�������烊���[�g�̏����擾

        dirInfo = this->getDirInfoByWinPath(START_CALLER argWinPath);
        if (!dirInfo)
        {
            traceW(L"fault: getFileInfoByFileName, argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }
    }

    HANDLE hFile = INVALID_HANDLE_VALUE;

    switch (dirInfo->FileType)
    {
        case FileTypeEnum::DirectoryObject:
        {
            traceW(L"argWinPath=%s [DIRECTORY]", argWinPath.c_str());

            [[fallthrough]];
        }
        case FileTypeEnum::Bucket:
        case FileTypeEnum::RootDirectory:
        {
            hFile = mRuntimeEnv->DirSecurityRef.handle();
            break;
        }

        case FileTypeEnum::FileObject:
        {
            traceW(L"argWinPath=%s [FILE]", argWinPath.c_str());

            hFile = mRuntimeEnv->FileSecurityRef.handle();
            break;
        }

        default:
        {
            APP_ASSERT(0);
            break;
        }
    }

    const auto ntstatus = HandleToSecurityInfo(hFile, argSecurityDescriptor, argSecurityDescriptorSize);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: HandleToSecurityInfo");
        return ntstatus;
    }

    if (pFileAttributes)
    {
        *pFileAttributes = dirInfo->FileInfo.FileAttributes;
    }

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::Open(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    // ���ɃI�[�v�����Ă�����̂�����΁A������̗p

    bool callAcquire = false;

    auto dirInfo{ mActiveDirInfo.get(argWinPath) };
    if (dirInfo)
    {
        // �ŏ��� acquire ���Ăяo���Ɗ֐����ŃG���[�����������Ƃ��� release ����
        // �K�v����������̂ŁA�����ł� get ���čŌ�� acquire ���Ăяo��

        callAcquire = true;
    }
    else
    {
        // �I�[�v�����Ă�����̂��Ȃ���΁AdirInfo ���擾

        dirInfo = this->getDirInfoByWinPath(START_CALLER argWinPath);
        if (!dirInfo)
        {
            traceW(L"fault: getFileInfoByFileName argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }
    }

    const auto optObjKey{ CSELIB::ObjectKey::fromWinPath(argWinPath) };

    std::unique_ptr<FileContext> ctx;

    if (dirInfo->FileType == FileTypeEnum::FileObject)
    {
        // �L���b�V���E�t�@�C���̑���(�T�C�Y�ȊO) �������[�g�Ɠ�������

        traceW(L"dirInfo=%s", dirInfo->str().c_str());

        std::filesystem::path cacheFilePath;
        if (!makeCacheFilePath(mRuntimeEnv->CacheDataDir, argWinPath, &cacheFilePath))
        {
            traceW(L"fault: makeCacheFilePath");
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        const auto ntstatus = syncAttributes(cacheFilePath, dirInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: syncRemoteAttributes");
            return ntstatus;
        }

        // �L���b�V���E�t�@�C�����J���R���e�N�X�g�ɕۑ�

        ULONG CreateFlags = 0;

        if (argCreateOptions & FILE_DELETE_ON_CLOSE)
            CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

        FileHandle file = ::CreateFileW(
            cacheFilePath.c_str(),
            argGrantedAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            CreateFlags,
            NULL);

        if (file.invalid())
        {
            traceW(L"fault: CreateFile cacheFilePath=%s", cacheFilePath.c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        ctx = std::make_unique<OpenFileContext>(argWinPath, dirInfo->FileType, &dirInfo->FileInfo, optObjKey, std::move(file));
    }
    else
    {
        // �o�P�b�g�A�f�B���N�g���̏ꍇ�͎Q�Ɨp�̃n���h����ݒ肵�ăR���e�N�X�g�𐶐�

        ctx = std::make_unique<RefFileContext>(argWinPath, dirInfo->FileType, &dirInfo->FileInfo, optObjKey, mRuntimeEnv->DirSecurityRef.handle());
    }

    if (callAcquire)
    {
        // �Q�ƃJ�E���g�𑝂₷

        if (!mActiveDirInfo.acquire(argWinPath))
        {
            traceW(L"fault: acquire");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        // �擾���� dirInfo ���I�[�v�����Ƃ��ēo�^

        if (!mActiveDirInfo.addAndAcquire(argWinPath, dirInfo))
        {
            traceW(L"fault: addAndAcquire");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // WinFsp �ɕԋp

    *pFileInfo = dirInfo->FileInfo;
    *pFileContext = ctx.release();

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::Create(const std::filesystem::path& argWinPath, UINT32 argCreateOptions,
    UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor,
    UINT64 argAllocationSize, FileContext** argFileContext, FSP_FSCTL_FILE_INFO* argFileInfo)
{
	return STATUS_INVALID_DEVICE_REQUEST;
}

VOID CSDriver::Close(FileContext* ctx)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=[%s]", ctx->str().c_str());

    if (ctx->mFlags & FCTX_FLAGS_MODIFY)
    {
        switch (ctx->mFileType)
        {
            case FileTypeEnum::FileObject:
            {
                if (ctx->getHandleWrite() == INVALID_HANDLE_VALUE)
                {
                    // �t�@�C���폜�̏ꍇ�� SetDelete �Ńt���O�������ACleanup �ŕ����Ă���

                    traceW(L"already closed");
                    break;
                }

                ::SwitchToThread();

                // ���_�E�����[�h�������擾����

                auto ntstatus = syncContent(this, ctx, 0, (FILEIO_LENGTH_T)ctx->mFileInfoRef->FileSize);
                if (!NT_SUCCESS(ntstatus))
                {
                    traceW(L"fault: syncContent");
                    break;
                }

                // �L���b�V���t�@�C���̃p�X���擾

                std::filesystem::path cacheFilePath;

                if (!GetFileNameFromHandle(ctx->getHandle(), &cacheFilePath))
                {
                    traceW(L"fault: GetFileNameFromHandle");
                    break;
                }

                // ���݃o�b�t�@�����O����Ă�����e���m�肳����

                if (!::FlushFileBuffers(ctx->getHandleWrite()))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: FlushFileBuffers lerr=%lu", lerr);
                    break;
                }

                // �A�b�v���[�h�̎��s

                if (!mDevice->putObject(START_CALLER *ctx->mOptObjKey, *ctx->mFileInfoRef, cacheFilePath.c_str()))
                {
                    traceW(L"fault: putObject");
                    break;
                }

                traceW(L"success: putObject mOptObjKey=%s", ctx->mOptObjKey->c_str());

                if (mRuntimeEnv->DeleteAfterUpload)
                {
                    // �A�b�v���[�h��Ƀt�@�C�����폜

                    if (!::DeleteFileW(cacheFilePath.c_str()))
                    {
                        const auto lerr = ::GetLastError();
                        traceW(L"fault: DeleteFileW lerr=%lu", lerr);
                        break;
                    }

                    traceW(L"success: DeleteFileW cacheFilePath=%s", cacheFilePath.c_str());
                }

                break;
            }
        }
    }

    // �I�[�v�����̏�񂩂�폜

    const bool b = mActiveDirInfo.release(ctx->mWinPath);
    APP_ASSERT(b);

    // Open() �Ő������������������

    delete ctx;
}

VOID CSDriver::Cleanup(FileContext* ctx, PWSTR argFileName, ULONG argFlags)
{
    if (argFlags & FspCleanupDelete)
    {
        mDevice->deleteObject(START_CALLER *ctx->mOptObjKey);


        ctx->closeHandle();
    }
}

NTSTATUS CSDriver::Flush(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    HANDLE Handle = ctx->getHandleWrite();

    /* we do not flush the whole volume, so just return SUCCESS */
    //if (0 == Handle)
    //    return STATUS_SUCCESS;

    if (!::FlushFileBuffers(Handle))
    {
        traceW(L"fault: FlushFileBuffers");
        return FspNtStatusFromWin32(::GetLastError());
    }

#if 1
    const auto ntstatus = updateFileInfo(Handle, ctx->mFileInfoRef);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: updateFileInfo");
        return ntstatus;
    }

    *pFileInfo = *ctx->mFileInfoRef;

    return STATUS_SUCCESS;

#else
    return GetFileInfoInternal(Handle, pFileInfo);

#endif
}

NTSTATUS CSDriver::GetFileInfo(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    *pFileInfo = *ctx->mFileInfoRef;

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::GetSecurity(FileContext* ctx, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T pSecurityDescriptorSize)
{
    NEW_LOG_BLOCK();

    DWORD SecurityDescriptorSizeNeeded = 0;

    if (!::GetKernelObjectSecurity(ctx->getHandle(),
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        argSecurityDescriptor, (DWORD)*pSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
    {
        traceW(L"fault: GetKernelObjectSecurity");

        *pSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
        return FspNtStatusFromWin32(::GetLastError());
    }

    *pSecurityDescriptorSize = SecurityDescriptorSizeNeeded;

    return STATUS_SUCCESS;
}

NTSTATUS CSDriver::Overwrite(FileContext* argFileContext, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* argFileInfo)
{
	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS CSDriver::Read(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred)
{
    NEW_LOG_BLOCK();

    // �����[�g�̓��e�ƕ������� (argOffset + argLengh �͈̔�)

    const auto ntstatus = syncContent(this, ctx, (FILEIO_OFFSET_T)argOffset, argLength);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: syncContent");
        return ntstatus;
    }

    HANDLE Handle = ctx->getHandleWrite();

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)argOffset;
    Overlapped.OffsetHigh = (DWORD)(argOffset >> 32);

    if (!::ReadFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
    {
        traceW(L"fault: ReadFile");
        return FspNtStatusFromWin32(::GetLastError());
    }

    return STATUS_SUCCESS;
}

//
// ���̃��\�b�h�ƈقȂ���N���X�� RelayReadDirectory() �ł͔r��������s�킸�A�����ɋL�q���Ă���B
// ����́A�r������̕K�v�ȑΏۂ��������݂��邽�߂ł���B
// 
//  1) �R���e�N�X�g�ɐݒ肳�ꂽ mFileName �ł̔r������            ... ���̊֐��Ɠ��`
//  2) ���X�g�����X�̃I�u�W�F�N�g���Ƃ̔r������
// 
// ����ɂ��A���̃��\�b�h�����p���Ă���I�u�W�F�N�g�Ɠ����ł���͂�
//
NTSTATUS CSDriver::ReadDirectory(FileContext* ctx, PWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred)
{
    StatsIncr(RelayReadDirectory);
    NEW_LOG_BLOCK();

    APP_ASSERT(ctx && argBuffer && argBytesTransferred);

    // �I�[�v�����̏����܂Ƃ߂Ď擾

    const auto activeDirInfo{ mActiveDirInfo.copy() };

    std::optional<std::wregex> re;

    if (argPattern)
    {
        // �����̃p�^�[���𐳋K�\���ɕϊ�

        re = WildcardToRegexW(argPattern);
    }

    // �f�B���N�g���̒��̈ꗗ�擾

    DirInfoPtrList dirInfoList;

    // 1) �R���e�N�X�g�� mFileName �ɂ��r������

    {
        UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->mWinPath };
        {
            const auto safeShare{ unsafeShare.lock() };

            switch (ctx->mFileType)
            {
                case FileTypeEnum::RootDirectory:
                {
                    APP_ASSERT(!ctx->mOptObjKey);

                    // "\" �ւ̃A�N�Z�X�̓o�P�b�g�ꗗ���

                    if (!mDevice->listBuckets(START_CALLER &dirInfoList))
                    {
                        traceW(L"not fouund/1");

                        return STATUS_OBJECT_NAME_INVALID;
                    }

                    if (dirInfoList.empty())
                    {
                        traceW(L"empty buckets");
                        return STATUS_SUCCESS;
                    }

                    break;
                }

                case FileTypeEnum::Bucket:
                case FileTypeEnum::DirectoryObject:
                {
                    APP_ASSERT(ctx->mOptObjKey);

                    // "\bucket" �܂��� "\bucket\key"

                    const auto listObjKey{ ctx->mOptObjKey->toDir() };
                    if (listObjKey.isObject())
                    {
                        traceW(L"listObjKey=%s", listObjKey.c_str());
                    }

                    // �L�[����̏ꍇ)		bucket & ""     �Ō���
                    // �L�[����łȂ��ꍇ)	bucket & "key/" �Ō���

                    if (!mDevice->listDisplayObjects(START_CALLER listObjKey, &dirInfoList))
                    {
                        traceW(L"not found/2");

                        return STATUS_OBJECT_NAME_INVALID;
                    }

                    // ���Ȃ��Ƃ� "." �͂���̂ŋ�ł͂Ȃ��͂�

                    //traceW(L"object count: %zu", dirInfoList.size());

                    break;
                }

                default:
                {
                    APP_ASSERT(0);

                    break;
                }
            }
        }
    }

    APP_ASSERT(!dirInfoList.empty());

    // �擾�������̂� WinFsp �ɓ]������

    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    if (FspFileSystemAcquireDirectoryBuffer(&ctx->mDirBuffer, 0 == argMarker, &ntstatus))
    {
        for (const auto& dirInfo: dirInfoList)
        {
            APP_ASSERT(!mDevice->shouldIgnoreFileName(dirInfo->FileNameBuf));

            if (re)
            {
                if (!std::regex_match(dirInfo->FileNameBuf, *re))
                {
                    continue;
                }
            }

            const auto winFullPath{ ctx->mWinPath / dirInfo->FileNameBuf };

            // 2) ���X�g�����X�̃I�u�W�F�N�g���Ƃ̔r������

            UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, winFullPath };
            {
                // �t�@�C�����Ŕr�����䂵�AFSP_FSCTL_DIR_INFO* ���ύX����Ȃ��悤�ɂ���

                const auto safeShare{ unsafeShare.lock() };

                const auto it{ activeDirInfo.find(winFullPath) };

                FSP_FSCTL_DIR_INFO* rawDirInfo = it == activeDirInfo.cend() ? dirInfo->data() : it->second->data();

                if (!FspFileSystemFillDirectoryBuffer(&ctx->mDirBuffer, rawDirInfo, &ntstatus))
                {
                    break;
                }
            }
        }

        FspFileSystemReleaseDirectoryBuffer(&ctx->mDirBuffer);
    }

    if (!NT_SUCCESS(ntstatus))
    {
        return ntstatus;
    }

    FspFileSystemReadDirectoryBuffer(&ctx->mDirBuffer, argMarker, argBuffer, argBufferLength, argBytesTransferred);

    return STATUS_SUCCESS;
}

NTSTATUS CSDriver::Rename(FileContext* argFileContext, PWSTR argFileName, PWSTR argNewFileName, BOOLEAN argReplaceIfExists)
{
	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS CSDriver::SetBasicInfo(FileContext* argFileContext, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* argFileInfo)
{
	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS CSDriver::SetFileSize(FileContext* ctx, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    // ���_�E�����[�h�������擾����

    auto ntstatus = syncContent(this, ctx, 0, (FILEIO_LENGTH_T)argNewSize);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: syncContent");
        return ntstatus;
    }

    HANDLE Handle = ctx->getHandleWrite();

    FILE_ALLOCATION_INFO AllocationInfo{};
    FILE_END_OF_FILE_INFO EndOfFileInfo{};

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

        if (!::SetFileInformationByHandle(Handle, FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
        {
            traceW(L"fault: SetFileInformationByHandle");
            return FspNtStatusFromWin32(::GetLastError());
        }
    }
    else
    {
        EndOfFileInfo.EndOfFile.QuadPart = argNewSize;

        if (!::SetFileInformationByHandle(Handle, FileEndOfFileInfo, &EndOfFileInfo, sizeof EndOfFileInfo))
        {
            traceW(L"fault: SetFileInformationByHandle");
            return FspNtStatusFromWin32(::GetLastError());
        }
    }

    ntstatus = GetFileInfoInternal(Handle, pFileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return ntstatus;
    }

    *ctx->mFileInfoRef = *pFileInfo;

    return STATUS_SUCCESS;
}

NTSTATUS CSDriver::SetSecurity(FileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor)
{
	return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS CSDriver::Write(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    // �����[�g�̓��e�ƕ������� (argOffset + argLengh �͈̔�)

    auto ntstatus = syncContent(this, ctx, (FILEIO_OFFSET_T)argOffset, (FILEIO_LENGTH_T)argLength);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: syncContent");
        return ntstatus;
    }

    HANDLE Handle = ctx->getHandleWrite();
    LARGE_INTEGER FileSize{};
    OVERLAPPED Overlapped{};

    if (argConstrainedIo)
    {
        if (!::GetFileSizeEx(Handle, &FileSize))
        {
            traceW(L"fault: GetFileSizeEx");
            return FspNtStatusFromWin32(::GetLastError());
        }

        if (static_cast<INT64>(argOffset) >= FileSize.QuadPart)
        {
            return STATUS_SUCCESS;
        }

        if (static_cast<INT64>(argOffset) + argLength > FileSize.QuadPart)
        {
            argLength = static_cast<ULONG>(FileSize.QuadPart - static_cast<INT64>(argOffset));
        }
    }

    Overlapped.Offset = static_cast<DWORD>(argOffset);
    Overlapped.OffsetHigh = static_cast<DWORD>(argOffset >> 32);

    if (!::WriteFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
    {
        traceW(L"fault: WriteFile");
        return FspNtStatusFromWin32(::GetLastError());
    }

#if 1
    // ActiveDirInfo �ɔ��f

    ntstatus = updateFileInfo(Handle, ctx->mFileInfoRef);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: updateFileInfo");
        return ntstatus;
    }

    *pFileInfo = *ctx->mFileInfoRef;

    return STATUS_SUCCESS;

#else
    return GetFileInfoInternal(Handle, pFileInfo);

#endif
}

NTSTATUS CSDriver::SetDelete(FileContext* ctx, PWSTR argFileName, BOOLEAN argDeleteFile)
{
    NEW_LOG_BLOCK();

    switch (ctx->mFileType)
    {
        case FileTypeEnum::DirectoryObject:
        {
            DirInfoPtrList dirInfoList;

            if (!mDevice->listObjects(START_CALLER *ctx->mOptObjKey, &dirInfoList))
            {
                traceW(L"fault: listObjects");
                return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
            }

            decltype(dirInfoList)::const_iterator it;

            switch (mRuntimeEnv->DeleteDirCondition)
            {
                case 1:
                {
                    // �T�u�f�B���N�g��������ꍇ�͍폜�s��

                    it = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
                    {
                        return wcscmp(dirInfo->FileNameBuf, L".") != 0
                            && wcscmp(dirInfo->FileNameBuf, L"..") != 0
                            && FA_IS_DIRECTORY(dirInfo->FileInfo.FileAttributes);
                    });

                    break;
                }

                case 2:
                {
                    // ��̃f�B���N�g���ȊO�͍폜�s��

                    it = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
                    {
                        return wcscmp(dirInfo->FileNameBuf, L".") != 0
                            && wcscmp(dirInfo->FileNameBuf, L"..") != 0;
                    });

                    break;
                }
            }

            if (it != dirInfoList.cend())
            {
                traceW(L"dir not empty");
                return STATUS_CANNOT_DELETE;
                //return STATUS_DIRECTORY_NOT_EMPTY;
            }

            break;
        }

        case FileTypeEnum::FileObject:
        {
            /*
            https://stackoverflow.com/questions/36217150/deleting-a-file-based-on-disk-id

            SetFileInformationByHandle �� FILE_DISPOSITION_INFO �Ƌ��Ɏg�p����ƁA
            �J���Ă���n���h�������t�@�C�����A���ׂẴn���h��������ꂽ�Ƃ��ɍ폜�����悤�ɐݒ�ł��܂��B
            */

            HANDLE Handle = ctx->getHandleWrite();

            FILE_DISPOSITION_INFO DispositionInfo{};

            DispositionInfo.DeleteFile = argDeleteFile;

            if (!::SetFileInformationByHandle(Handle, FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
            {
                traceW(L"fault: SetFileInformationByHandle");
                return FspNtStatusFromWin32(::GetLastError());
            }

            break;
        }
    }

    return STATUS_SUCCESS;
}

// EOF