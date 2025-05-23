#include "CSDriver.hpp"

#define GET_MIME_TYPE (0)

#if GET_MIME_TYPE
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#endif

using namespace CSELIB;

namespace CSEDRV {

NTSTATUS CSDriver::GetSecurityByName(const std::filesystem::path& argWinPath, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize)
{
    NEW_LOG_BLOCK();

    // OpenDirEntry ����擾

    traceW(L"argWinPath=%s", argWinPath.c_str());

    auto dirEntry{ mOpenDirEntry.get(argWinPath) };
    if (!dirEntry)
    {
        if (mDevice->shouldIgnoreWinPath(argWinPath))
        {
            // "desktop.ini" �Ȃǂ͖���������

            traceW(L"ignore argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        // �t�@�C�������烊���[�g�̏����擾

        dirEntry = this->getDirEntryByWinPath(START_CALLER argWinPath);
        if (!dirEntry)
        {
            traceW(L"fault: getDirEntryByWinPath, argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }
    }

    HANDLE hFile = INVALID_HANDLE_VALUE;

    switch (dirEntry->mFileType)
    {
        case FileTypeEnum::Directory:
        {
            traceW(L"argWinPath=%s [DIRECTORY]", argWinPath.c_str());

            [[fallthrough]];
        }
        case FileTypeEnum::Root:
        case FileTypeEnum::Bucket:
        {
            hFile = mRuntimeEnv->DirSecurityRef.handle();
            break;
        }

        case FileTypeEnum::File:
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
        errorW(L"fault: HandleToSecurityInfo argWinPath=%s", argWinPath.c_str());
        return ntstatus;
    }

    if (pFileAttributes)
    {
        *pFileAttributes = dirEntry->mFileInfo.FileAttributes;
    }

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::Open(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argWinPath=%s argCreateOptions=%u argGrantedAccess=%u", argWinPath.c_str(), argCreateOptions, argGrantedAccess);

    // ���ɃI�[�v�����Ă�����̂�����΁A������̗p

    bool addRefCount = false;

    auto dirEntry{ mOpenDirEntry.get(argWinPath) };
    if (dirEntry)
    {
        // �ŏ��� acquire ���Ăяo���Ɗ֐����ŃG���[�����������Ƃ��� release ����
        // �K�v����������̂ŁA�����ł� get ���čŌ�� acquire ���Ăяo��

        addRefCount = true;
    }
    else
    {
        if (mDevice->shouldIgnoreWinPath(argWinPath))
        {
            // "desktop.ini" �Ȃǂ͖���������
            // --> GetSecurityByName �ŋ��ۂ��Ă���̂ł����͒ʉ߂��Ȃ��͂�

            traceW(L"ignore argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        // �I�[�v�����Ă�����̂��Ȃ���΁AdirEntry ���擾

        dirEntry = this->getDirEntryByWinPath(START_CALLER argWinPath);
        if (!dirEntry)
        {
            errorW(L"fault: getDirEntryByWinPath argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }
    }

    traceW(L"addRefCount=%s dirEntry=%s", BOOL_CSTRW(addRefCount), dirEntry->str().c_str());

    // �R���e�N�X�g�̍쐬

    std::unique_ptr<FileContext> ctx;

    switch (dirEntry->mFileType)
    {
        case FileTypeEnum::Root:
        case FileTypeEnum::Bucket:
        {
            // �Q�Ɨp�̃n���h����ݒ肵�ăR���e�N�X�g�𐶐�

            ctx = std::make_unique<RefFileContext>(argWinPath, dirEntry, mRuntimeEnv->DirSecurityRef.handle());

            break;
        }

        case FileTypeEnum::Directory:
        {
            // �Q�Ɨp�̃n���h����ݒ肵�ăR���e�N�X�g�𐶐�

            ctx = std::make_unique<RefFileContext>(argWinPath, dirEntry, mRuntimeEnv->DirSecurityRef.handle());

            break;
        }

        case FileTypeEnum::File:
        {
            // �L���b�V���t�@�C���̑���(�T�C�Y�ȊO) �������[�g�Ɠ�������

            std::filesystem::path cacheFilePath;

            if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, argWinPath, &cacheFilePath))
            {
                errorW(L"fault: resolveCacheFilePath argWinPath=%s", argWinPath.c_str());
                return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
            }

            const auto ntstatus = syncAttributes(dirEntry, cacheFilePath);
            if (!NT_SUCCESS(ntstatus))
            {
                errorW(L"fault: syncRemoteAttributes dirEntry=%s", dirEntry->str().c_str());
                return ntstatus;
            }

            // �L���b�V���t�@�C�����J���R���e�N�X�g�ɕۑ�

            ULONG CreateFlags = 0;

            if (argCreateOptions & FILE_DELETE_ON_CLOSE)
            {
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
            }

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
                const auto lerr = ::GetLastError();

                errorW(L"fault: CreateFile lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
                return FspNtStatusFromWin32(lerr);
            }

            ctx = std::make_unique<OpenFileContext>(argWinPath, dirEntry, std::move(file));

            break;
        }
    }

    traceW(L"ctx=%s", ctx->str().c_str());

    if (addRefCount)
    {
        // �Q�ƃJ�E���g�𑝂₷

        if (!mOpenDirEntry.acquire(argWinPath))
        {
            errorW(L"fault: acquire argWinPath=%s", argWinPath.c_str());
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        // �擾���� dirEntry ���I�[�v�����Ƃ��ēo�^

        if (!mOpenDirEntry.addAndAcquire(argWinPath, dirEntry))
        {
            errorW(L"fault: addAndAcquire argWinPath=%s", argWinPath.c_str());
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // WinFsp �ɕԋp

    *pFileInfo = dirEntry->mFileInfo;
    *pFileContext = ctx.release();

	return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Create(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argWinPath=%s argCreateOptions=%u argCreateOptions=%u argFileAttributes=%u argAllocationSize=%llu", argWinPath.c_str(), argCreateOptions, argCreateOptions, argFileAttributes, argAllocationSize);
    traceW(L"argFileAttributes=%s", FileAttributesToStringW(argFileAttributes).c_str());

    if (mDevice->shouldIgnoreWinPath(argWinPath))
    {
        traceW(L"ignore argWinPath=%s", argWinPath.c_str());
        return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
    }

    const bool isDir = argCreateOptions & FILE_DIRECTORY_FILE;

    // �����̃t�@�C���������ɑ��݂��Ă��邩���m�F

    std::optional<ObjectKey> optObjKey;

    const auto ntstatus = this->canCreateObject(START_CALLER argWinPath, isDir, &optObjKey);
    if (!NT_SUCCESS(ntstatus))
    {
        if (ntstatus != STATUS_OBJECT_NAME_COLLISION)
        {
            errorW(L"fault: canCreateObject argWinPath=%s ntstatus=%ld", argWinPath.c_str(), ntstatus);
        }

        return ntstatus;
    }

    // �f�B���N�g���E�G���g���̍쐬

    const auto objKey{ *optObjKey };

    std::wstring filename;
    if (!SplitObjectKey(objKey.str(), nullptr, &filename))
    {
        errorW(L"fault: SplitObjectKey objKey=%s", objKey.c_str());
        return STATUS_OBJECT_NAME_INVALID;
    }

    const auto fileTime = GetCurrentWinFileTime100ns();

    const auto dirEntry = isDir
        ? DirectoryEntry::makeDirectoryEntry(filename, fileTime)
        : DirectoryEntry::makeFileEntry(filename, 0, fileTime);

    traceW(L"dirEntry=%s", dirEntry->str().c_str());

    // �R���e�N�X�g�̍쐬

    const auto dirInfoPtr{ dirEntry->makeDirInfo() };

    std::unique_ptr<FileContext> ctx;

    if (isDir)
    {
        // �����[�g�Ƀf�B���N�g�����쐬

        if (!mDevice->putObject(START_CALLER objKey, dirInfoPtr->FileInfo, nullptr))
        {
            errorW(L"fault: putObject objKey=%s", objKey.c_str());

            return STATUS_INVALID_DEVICE_REQUEST;
        }

        ctx = std::make_unique<RefFileContext>(argWinPath, dirEntry, mRuntimeEnv->DirSecurityRef.handle());
    }
    else
    {
        // �L���b�V���t�@�C���̍쐬

        std::filesystem::path cacheFilePath;

        if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, argWinPath, &cacheFilePath))
        {
            errorW(L"fault: resolveCacheFilePath argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
        }

        ULONG CreateFlags = 0;

        if (argCreateOptions & FILE_DELETE_ON_CLOSE)
        {
            CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
        }

        argFileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

        if (0 == argFileAttributes)
        {
            argFileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

        FileHandle file = ::CreateFileW(
            cacheFilePath.c_str(),
            argGrantedAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            CREATE_ALWAYS,
            CreateFlags | argFileAttributes,
            NULL);

        if (file.invalid())
        {
            errorW(L"fault: CreateFileW cacheFilePath=%s", cacheFilePath.c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        ctx = std::make_unique<OpenFileContext>(argWinPath, dirEntry, std::move(file));
    }

    traceW(L"ctx=%s", ctx->str().c_str());

    // �擾���� dirEntry ���I�[�v�����Ƃ��ēo�^

    if (!mOpenDirEntry.addAndAcquire(argWinPath, dirEntry))
    {
        errorW(L"fault: addAndAcquire argWinPath=%s", argWinPath.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // WinFsp �ɕԋp

    *pFileInfo = dirEntry->mFileInfo;
    *pFileContext = ctx.release();

	return STATUS_SUCCESS;
}

void CSDriver::UploadWhenClosing(CALLER_ARG FileContext* ctx)
{
    NEW_LOG_BLOCK();

    // ���e�ɕύX������A�����[�g�ւ̔��f���K�v�ȏ��

    const auto& dirEntry{ ctx->getDirEntry() };
    const auto objKey{ ctx->getObjectKey() };

    switch (dirEntry->mFileType)
    {
        case FileTypeEnum::File:
        {
            //::SwitchToThread();

            // �L���b�V���t�@�C���̃p�X���擾

            std::filesystem::path cacheFilePath;

            if (!GetFileNameFromHandle(ctx->getHandle(), &cacheFilePath))
            {
                errorW(L"fault: GetFileNameFromHandle ctx=%s", ctx->str().c_str());
                break;
            }

            // ���_�E�����[�h�������擾����

            auto ntstatus = this->syncContent(CONT_CALLER ctx, 0, (FILEIO_LENGTH_T)dirEntry->mFileInfo.FileSize);
            if (!NT_SUCCESS(ntstatus))
            {
                errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
                break;
            }

            // �A�b�v���[�h�̎��s

            if (!mDevice->putObject(START_CALLER objKey, dirEntry->mFileInfo, cacheFilePath.c_str()))
            {
                errorW(L"fault: putObject objKey=%s", objKey.c_str());
                break;
            }

            traceW(L"success: putObject objKey=%s", objKey.c_str());

            // �L���b�V���̍X�V
            // robocopy �΍�

            mDevice->headObject(START_CALLER objKey, nullptr);

            if (mRuntimeEnv->DeleteAfterUpload)
            {
                // �A�b�v���[�h��Ƀt�@�C�����폜

                if (!::DeleteFileW(cacheFilePath.c_str()))
                {
                    const auto lerr = ::GetLastError();
                    errorW(L"fault: DeleteFileW lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
                }

                traceW(L"success: DeleteFileW cacheFilePath=%s", cacheFilePath.c_str());
            }

            break;
        }
    }
}

VOID CSDriver::Close(FileContext* ctx)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    if (ctx->getHandle() == INVALID_HANDLE_VALUE)
    {
        // SetDelete �ō폜�t���O�������ACleanup �ŕ����Ă���

        traceW(L"already closed");
    }
    else if (ctx->mFlags & FCTX_FLAGS_MODIFY)
    {
        this->UploadWhenClosing(START_CALLER ctx);
    }

    // �I�[�v�����̏�񂩂�폜

    const bool b = mOpenDirEntry.release(ctx->getWinPath());
    if (!b)
    {
        traceW(L"fault: already released");
    }

    // Open() �Ő������������������

    delete ctx;
}

#pragma warning(suppress: 4100)
VOID CSDriver::Cleanup(FileContext* ctx, PCWSTR argWinPath, ULONG argFlags)
{
    NEW_LOG_BLOCK();

    traceW(L"argWinPath=%s argFlags=%lu ctx=%s", argWinPath, argFlags, ctx->str().c_str());

    if (argFlags & FspCleanupDelete)
    {
        const auto& refWinPath{ ctx->getWinPath() };

        switch (ctx->getDirEntry()->mFileType)
        {
            case FileTypeEnum::Directory:
            {
                // ���܂�Ӗ��͂Ȃ����ꉞ :-)

                traceW(L"closeHandle");
                ctx->closeHandle();

                break;
            }

            case FileTypeEnum::File:
            {
                std::filesystem::path cacheFilePath;
                if (resolveCacheFilePath(mRuntimeEnv->CacheDataDir, refWinPath, &cacheFilePath))
                {
                    const auto faBefore = ::GetFileAttributesW(cacheFilePath.c_str());

                    traceW(L"closeHandle");
                    ctx->closeHandle();

                    const auto faAfter = ::GetFileAttributesW(cacheFilePath.c_str());

                    if (faBefore != INVALID_FILE_ATTRIBUTES && faAfter == INVALID_FILE_ATTRIBUTES)
                    {
                        // �n���h�����N���[�Y�������Ƃő������ω�����
                        // --> FILE_FLAG_DELETE_ON_CLOSE �̉e���ɂ��L���b�V���t�@�C�����폜���ꂽ�Ƃ�

                        // �����[�g�̍폜

                        if (!mDevice->deleteObject(START_CALLER ctx->getObjectKey()))
                        {
                            errorW(L"fault: deleteObject");
                        }
                    }
                }
                else
                {
                    errorW(L"fault: resolveCacheFilePath refWinPath=%s", refWinPath.c_str());

                    traceW(L"closeHandle");
                    ctx->closeHandle();
                }

                break;
            }
        }

        // �I�[�v�����̏�񂩂�폜

        const bool b = mOpenDirEntry.release(refWinPath);
        if (!b)
        {
            traceW(L"fault: mOpenDirEntry.release");
        }
    }
}

NTSTATUS CSDriver::Flush(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    // MS EXCEL ��V�K�쐬����ƒʉ߂���

    traceW(L"ctx=%s", ctx->str().c_str());

    HANDLE Handle = ctx->getWritableHandle();

    /* we do not flush the whole volume, so just return SUCCESS */
    //if (0 == Handle)
    //    return STATUS_SUCCESS;

    if (!::FlushFileBuffers(Handle))
    {
        errorW(L"fault: FlushFileBuffers ctx=%s", ctx->str().c_str());
        return FspNtStatusFromWin32(::GetLastError());
    }

    // �t�@�C���T�C�Y�ɕύX�͂Ȃ��̂ŁAWrite �Ɠ��������ŗǂ��͂� (true)

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, true);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

NTSTATUS CSDriver::GetFileInfo(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    *pFileInfo = ctx->getDirEntry()->mFileInfo;

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::GetSecurity(FileContext* ctx, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T pSecurityDescriptorSize)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    DWORD SecurityDescriptorSizeNeeded = 0;

    if (!::GetKernelObjectSecurity(ctx->getHandle(),
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        argSecurityDescriptor, (DWORD)*pSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
    {
        errorW(L"fault: GetKernelObjectSecurity ctx=%s", ctx->str().c_str());

        *pSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
        return FspNtStatusFromWin32(::GetLastError());
    }

    *pSecurityDescriptorSize = SecurityDescriptorSizeNeeded;

    return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Overwrite(FileContext* ctx, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    HANDLE Handle = ctx->getWritableHandle();

    FILE_BASIC_INFO BasicInfo{};
    FILE_ALLOCATION_INFO AllocationInfo{};
    FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo{};

    if (argReplaceFileAttributes)
    {
        if (0 == argFileAttributes)
        {
            argFileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

        BasicInfo.FileAttributes = argFileAttributes;

        if (!::SetFileInformationByHandle(Handle, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
        {
            errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }
    }
    else if (0 != argFileAttributes)
    {
        if (!::GetFileInformationByHandleEx(Handle, FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
        {
            errorW(L"fault: GetFileInformationByHandleEx ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        BasicInfo.FileAttributes = argFileAttributes | AttributeTagInfo.FileAttributes;

        if (BasicInfo.FileAttributes ^ argFileAttributes)
        {
            if (!::SetFileInformationByHandle(Handle, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
            {
                errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
                return FspNtStatusFromWin32(::GetLastError());
            }
        }
    }

    if (!::SetFileInformationByHandle(Handle, FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
    {
        errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
        return FspNtStatusFromWin32(::GetLastError());
    }

    // �L���b�V���t�@�C�����؂�l�߂��Ă���̂ŁA�t�@�C������D��(false)����

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, false);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

NTSTATUS CSDriver::Read(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    // �����[�g�̓��e�ƕ������� (argOffset + argLengh �͈̔�)

    const auto ntstatus = this->syncContent(START_CALLER ctx, (FILEIO_OFFSET_T)argOffset, argLength);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    HANDLE Handle = ctx->getWritableHandle();

    OVERLAPPED Overlapped{};

    Overlapped.Offset     = static_cast<DWORD>(argOffset);
    Overlapped.OffsetHigh = static_cast<DWORD>(argOffset >> 32);

    traceW(L"ReadFile argOffset=%llu argLength=%lu", argOffset, argLength);

    if (!::ReadFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();

        if (lerr == ERROR_HANDLE_EOF)
        {
            traceW(L"EOF");
        }
        else
        {
            errorW(L"fault: ReadFile ctx=%s", ctx->str().c_str());
        }

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"success: ReadFile argBytesTransferred=%lu", *argBytesTransferred);

#if GET_MIME_TYPE
    if (argOffset == 0 && *argBytesTransferred)
    {
        // �t�@�C���̐擪�������� Content-Type ���Z�o���ăv���p�e�B�ɐݒ�

        auto& props{ ctx->getDirEntry()->mUserProperties };

        if (props.find(L"wincse-content-type") == props.cend())
        {
            LPWSTR mimeType = nullptr;

            HRESULT hr = ::FindMimeFromData(nullptr, nullptr, argBuffer, *argBytesTransferred, nullptr, 0, &mimeType, 0);
            if (SUCCEEDED(hr))
            {
                props.insert({ L"wincse-content-type", mimeType });

                ::CoTaskMemFree(mimeType);
            }
        }
    }
#endif

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
NTSTATUS CSDriver::ReadDirectory(FileContext* ctx, PCWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred)
{
    NEW_LOG_BLOCK();

    traceW(L"argPattern=%s argMarker=%s ctx=%s", argPattern, argMarker, ctx->str().c_str());

    // �I�[�v�����̏��̂����A�e�f�B���N�g������v������̂��擾

    const auto& refWinPath{ ctx->getWinPath() };

    const auto& is_same_dir = [&refWinPath](const OpenDirEntry::copy_type::value_type& value)
    {
        if (refWinPath == L"\\")
        {
            return false;
        }

        if (refWinPath == value.first)
        {
            return false;
        }

        const auto parentPath{ value.first.parent_path() };

        return refWinPath == parentPath;
    };

    const auto openDirEntry{ mOpenDirEntry.copy_if(is_same_dir) };

    std::optional<std::wregex> reWildcard;

    if (argPattern)
    {
        // �����̃p�^�[���𐳋K�\���ɕϊ�

        reWildcard = WildcardToRegexW(argPattern);
    }

    // �f�B���N�g���̒��̈ꗗ�擾

    DirEntryListType dirEntryList;

    // 1) �R���e�N�X�g�� mFileName �ɂ��r������

    {
        UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
        {
            const auto safeShare{ unsafeShare.lock() };

            bool isBucket = false;

            switch (ctx->getDirEntry()->mFileType)
            {
                case FileTypeEnum::Root:
                {
                    // "\" �ւ̃A�N�Z�X�̓o�P�b�g�ꗗ���

                    if (!mDevice->listBuckets(START_CALLER &dirEntryList))
                    {
                        errorW(L"fault: listBuckets ctx=%s", ctx->str().c_str());

                        return STATUS_OBJECT_NAME_INVALID;
                    }

                    break;
                }

                case FileTypeEnum::Bucket:
                {
                    isBucket = true;

                    [[fallthrough]];
                }
                case FileTypeEnum::Directory:
                {
                    // "\bucket" �܂��� "\bucket\key"

                    const auto objKey{ ctx->getObjectKey() };

                    if (!mDevice->listDisplayObjects(START_CALLER objKey, &dirEntryList))
                    {
                        errorW(L"fault: listDisplayObjects objKey=%s", objKey.c_str());

                        return STATUS_OBJECT_NAME_INVALID;
                    }

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

    if (dirEntryList.empty())
    {
        traceW(L"empty buckets");
        return STATUS_SUCCESS;
    }

    // �擾�������̂� WinFsp �ɓ]������

    const auto dirInfoAllocSize = sizeof(FSP_FSCTL_DIR_INFO) + (MAX_PATH * sizeof(WCHAR));
    std::vector<BYTE> dirInfoBuf(dirInfoAllocSize);
    FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)dirInfoBuf.data();

    NTSTATUS fspNtstatus = STATUS_UNSUCCESSFUL;

    if (FspFileSystemAcquireDirectoryBuffer(&ctx->mDirBuffer, 0 == argMarker, &fspNtstatus))
    {
        std::set<std::filesystem::path> already;

        for (const auto& dirEntry: dirEntryList)
        {
            const std::wstring& fileNameBuf{ dirEntry->getFileNameBuf() };

            if (reWildcard)
            {
                if (!std::regex_match(fileNameBuf, *reWildcard))
                {
                    traceW(L"reWildcard no match fileNameBuf=%s", fileNameBuf.c_str());
                    continue;
                }
            }

            const auto winPath{ refWinPath / fileNameBuf };

            if (mDevice->shouldIgnoreWinPath(winPath))
            {
                traceW(L"ignore winPath=%s", winPath.c_str());
                continue;
            }

            traceW(L"winPath=%s", winPath.c_str());

            // 2) ���X�g�����X�̃I�u�W�F�N�g���Ƃ̔r������

            UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, winPath };
            {
                // �t�@�C�����Ŕr�����䂵�AFSP_FSCTL_DIR_INFO* ���ύX����Ȃ��悤�ɂ���

                const auto safeShare{ unsafeShare.lock() };

                // �I�[�v�����̃f�B���N�g���G���g��������΂�����A�����łȂ���΃��X�g���ꂽ�I�u�W�F�N�g
                // �̃f�B���N�g���G���g����I������ FSP_FSCTL_DIR_INFO �𐶐�

                const auto it{ openDirEntry.find(winPath) };

                if (it == openDirEntry.cend())
                {
                    memset(dirInfo, 0, dirInfoAllocSize);
                    dirEntry->getDirInfo(dirInfo);
                }
                else
                {
                    memset(dirInfo, 0, dirInfoAllocSize);
                    it->second->getDirInfo(dirInfo);
                }
                APP_ASSERT(dirInfo->FileInfo.FileAttributes);

                // readonly �����𔽉f

                this->applyDefaultFileAttributes(&dirInfo->FileInfo);

                // WinFsp �ɑ��M

                if (!FspFileSystemFillDirectoryBuffer(&ctx->mDirBuffer, dirInfo, &fspNtstatus))
                {
                    errorW(L"fault: FspFileSystemFillDirectoryBuffer ctx=%s", ctx->str().c_str());
                    break;
                }
            }

            already.insert(winPath);
        }

        for (const auto& it: openDirEntry)
        {
            if (already.find(it.first) != already.cend())
            {
                // ��L�œo�^��

                traceW(L"already: it.first=%s", it.first.c_str());
                continue;
            }

            traceW(L"new file it.first=%s", it.first.c_str());

            UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, it.first };
            {
                // �t�@�C�����Ŕr�����䂵�AFSP_FSCTL_DIR_INFO* ���ύX����Ȃ��悤�ɂ���

                const auto safeShare{ unsafeShare.lock() };

                // �V�K�쐬���ꂽ�t�@�C��&�f�B���N�g��

                memset(dirInfo, 0, dirInfoAllocSize);
                it.second->getDirInfo(dirInfo);

                if (!FspFileSystemFillDirectoryBuffer(&ctx->mDirBuffer, dirInfo, &fspNtstatus))
                {
                    errorW(L"fault: FspFileSystemFillDirectoryBuffer ctx=%s", ctx->str().c_str());
                    break;
                }
            }
        }

        FspFileSystemReleaseDirectoryBuffer(&ctx->mDirBuffer);
    }
    else
    {
        traceW(L"fault: FspFileSystemAcquireDirectoryBuffer argMarker=%s", argMarker);
    }

    if (!NT_SUCCESS(fspNtstatus))
    {
        errorW(L"fault: FspFilesystem*** ctx=%s", ctx->str().c_str());
        return fspNtstatus;
    }

    FspFileSystemReadDirectoryBuffer(&ctx->mDirBuffer, argMarker, argBuffer, argBufferLength, argBytesTransferred);

    return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Rename(FileContext* ctx, const std::filesystem::path& argSrcWinPath, const std::filesystem::path& argDstWinPath, BOOLEAN argReplaceIfExists)
{
    NEW_LOG_BLOCK();

    traceW(L"argSrcWinPath=%s argDstWinPath=%s argReplaceIfExists=%s ctx=%s", argSrcWinPath.c_str(), argDstWinPath.c_str(), BOOL_CSTRW(argReplaceIfExists), ctx->str().c_str());

    if (mDevice->shouldIgnoreWinPath(argDstWinPath))
    {
        traceW(L"ignore argDstWinPath=%s", argDstWinPath.c_str());
        return STATUS_ACCESS_DENIED;
    }

    const bool isDir = FA_IS_DIR(ctx->getDirEntry()->mFileInfo.FileAttributes);

    std::optional<ObjectKey> optDstObjKey;

    // ���l�[����̖��O�����݂��邩�m�F

    auto ntstatus = this->canCreateObject(START_CALLER argDstWinPath, isDir, &optDstObjKey);
    if (!NT_SUCCESS(ntstatus))
    {
        if (ntstatus == STATUS_OBJECT_NAME_COLLISION)
        {
            traceW(L"already exists: argDstWinPath=%s", argDstWinPath.c_str());
        }
        else
        {
            errorW(L"fault: canCreateObject argDstWinPath=%s", argDstWinPath.c_str());
        }

        return ntstatus;
    }

    const auto srcObjKey{ ctx->getObjectKey() };

    traceW(L"srcObjKey=%s", srcObjKey.c_str());

    if (isDir)
    {
        // �f�B���N�g���̏ꍇ�͋�̎��̂݃��l�[����

        DirEntryListType dirEntryList;

        if (!mDevice->listObjects(START_CALLER srcObjKey, &dirEntryList))
        {
            errorW(L"fault: listObjects srcObjKey=%s", srcObjKey.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        if (!dirEntryList.empty())
        {
            traceW(L"not empty srcObjKey=%s", srcObjKey.c_str());
            return STATUS_DIRECTORY_NOT_EMPTY;
        }
    }

    const auto dstObjKey{ *optDstObjKey };

    traceW(L"dstObjKey=%s", dstObjKey.c_str());

    const auto& srcFileInfo{ ctx->getDirEntry()->mFileInfo };

    // ���l�[����̃f�B���N�g���G���g�����쐬

    std::wstring filename;
    if (!SplitObjectKey(dstObjKey.str(), nullptr, &filename))
    {
        errorW(L"fault: SplitObjectKey dstObjKey=%s", dstObjKey.c_str());
        return STATUS_OBJECT_NAME_INVALID;
    }

    const auto dstDirEntry = isDir
        ? DirectoryEntry::makeDirectoryEntry(filename,                       srcFileInfo.CreationTime, srcFileInfo.LastAccessTime, srcFileInfo.LastWriteTime, srcFileInfo.ChangeTime)
        : DirectoryEntry::makeFileEntry(     filename, srcFileInfo.FileSize, srcFileInfo.CreationTime, srcFileInfo.LastAccessTime, srcFileInfo.LastWriteTime, srcFileInfo.ChangeTime);

    traceW(L"dstDirEntry=%s", dstDirEntry->str().c_str());

    const auto dstDirInfoPtr{ dstDirEntry->makeDirInfo() };

    // ���l�[�������I�[�v�����̏�񂩂�폜

    const bool b = mOpenDirEntry.release(ctx->getWinPath());
    if (!b)
    {
        traceW(L"fault: already released");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // �����[�g�̃I�u�W�F�N�g���R�s�[

    if (!mDevice->copyObject(START_CALLER srcObjKey, dstObjKey))
    {
        errorW(L"fault: copyObject copyObject=%s dstObjKey=%s", srcObjKey.c_str(), dstObjKey.c_str());
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    if (!isDir)
    {
        // �L���b�V���E�t�@�C������ύX

        // ���l�[�����̃L���b�V���E�t�@�C�������擾

        std::filesystem::path orgCacheFilePath;

        if (!GetFileNameFromHandle(ctx->getHandle(), &orgCacheFilePath))
        {
            errorW(L"fault: GetFileNameFromHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        // ���l�[����̃L���b�V���E�t�@�C�������쐬

        std::filesystem::path dstCacheFilePath;

        if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, argDstWinPath, &dstCacheFilePath))
        {
            errorW(L"fault: resolveCacheFilePath argDstWinPath=%s", argDstWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
        }

        // �L���b�V���E�t�@�C���̃��l�[��

        traceW(L"MoveFileExW orgCacheFilePath=%s, dstCacheFilePath=%s", orgCacheFilePath.c_str(), dstCacheFilePath.c_str());

        if (!::MoveFileExW(orgCacheFilePath.c_str(), dstCacheFilePath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const auto lerr = ::GetLastError();
            errorW(L"fault: MoveFileExW lerr=%lu orgCacheFilePath=%s, dstCacheFilePath=%s", lerr, orgCacheFilePath.c_str(), dstCacheFilePath.c_str());

            return FspNtStatusFromWin32(lerr);
        }
    }

    // �����[�g�̃��l�[�������폜

    traceW(L"deleteObject srcObjKey=%s", srcObjKey.c_str());

    if (!mDevice->deleteObject(START_CALLER srcObjKey))
    {
        errorW(L"fault: deleteObject srcObjKey=%s", srcObjKey.c_str());
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    // �R���e�N�X�g���̓���ւ�

    ctx->rename(argDstWinPath, dstDirEntry);

    // ���l�[������I�[�v�����Ƃ��ēo�^

    if (!mOpenDirEntry.addAndAcquire(argDstWinPath, dstDirEntry))
    {
        errorW(L"fault: addAndAcquire argDstWinPath=%s", argDstWinPath.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

	return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::SetBasicInfo(FileContext* ctx, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argFileAttributes=%u argCreationTime=%llu argLastAccessTime=%llu argLastWriteTime=%llu argChangeTime=%llu ctx=%s",
        argFileAttributes, argCreationTime, argLastAccessTime, argLastWriteTime, argChangeTime, ctx->str().c_str());

    switch (ctx->getDirEntry()->mFileType)
    {
        case FileTypeEnum::Directory:
        {
            *pFileInfo = ctx->getDirEntry()->mFileInfo;

            return STATUS_SUCCESS;

            break;
        }

        case FileTypeEnum::File:
        {
            HANDLE Handle = ctx->getWritableHandle();

            FILE_BASIC_INFO BasicInfo{};

            if (INVALID_FILE_ATTRIBUTES == argFileAttributes)
            {
                argFileAttributes = 0;
            }
            else if (0 == argFileAttributes)
            {
                argFileAttributes = FILE_ATTRIBUTE_NORMAL;
            }

            BasicInfo.FileAttributes = argFileAttributes;
            BasicInfo.CreationTime.QuadPart = argCreationTime;
            BasicInfo.LastAccessTime.QuadPart = argLastAccessTime;
            BasicInfo.LastWriteTime.QuadPart = argLastWriteTime;
            //BasicInfo.ChangeTime = argChangeTime;

            if (!::SetFileInformationByHandle(Handle, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
            {
                errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
                return FspNtStatusFromWin32(::GetLastError());
            }

            // �t�@�C���T�C�Y�ɕύX�͂Ȃ��̂� Write �Ɠ�������(true)

            return this->updateFileInfo(START_CALLER ctx, pFileInfo, true);
            //return GetFileInfoInternal(Handle, pFileInfo);

            break;
        }
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS CSDriver::SetFileSize(FileContext* ctx, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argNewSize=%llu argSetAllocationSize=%s ctx=%s", argNewSize, BOOL_CSTRW(argSetAllocationSize), ctx->str().c_str());

    // ���_�E�����[�h�������擾����

    auto ntstatus = this->syncContent(START_CALLER ctx, 0, (FILEIO_LENGTH_T)argNewSize);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    HANDLE Handle = ctx->getWritableHandle();

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
            errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }
    }
    else
    {
        EndOfFileInfo.EndOfFile.QuadPart = argNewSize;

        if (!::SetFileInformationByHandle(Handle, FileEndOfFileInfo, &EndOfFileInfo, sizeof EndOfFileInfo))
        {
            errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }
    }

    // �t�@�C���T�C�Y�̕ύX�ɕK�v�ȕ����͓�������Ă���A���[�J���̃t�@�C���T�C�Y��D��(false)����

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, false);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::SetSecurity(FileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor)
{
	return STATUS_INVALID_DEVICE_REQUEST;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Write(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    // �����[�g�̓��e�ƕ������� (argOffset + argLengh �͈̔�)

    auto ntstatus = this->syncContent(START_CALLER ctx, (FILEIO_OFFSET_T)argOffset, (FILEIO_LENGTH_T)argLength);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    HANDLE Handle = ctx->getWritableHandle();
    LARGE_INTEGER FileSize{};
    OVERLAPPED Overlapped{};

    if (argConstrainedIo)
    {
        if (!::GetFileSizeEx(Handle, &FileSize))
        {
            errorW(L"fault: GetFileSizeEx ctx=%s", ctx->str().c_str());
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

    traceW(L"WriteFile argOffset=%llu argLength=%lu", argOffset, argLength);

    if (!::WriteFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
    {
        errorW(L"fault: WriteFile ctx=%s", ctx->str().c_str());
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"success: WriteFile argBytesTransferred=%lu", *argBytesTransferred);

    // Write �ɕK�v�ȕ����̂ݓ������Ă��邽�߁A�����[�g�̃t�@�C���T�C�Y��D��(true)����

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, true);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::SetDelete(FileContext* ctx, PCWSTR argFileName, BOOLEAN argDeleteFile)
{
    NEW_LOG_BLOCK();

    traceW(L"argFileName=%s argDeleteFile=%s ctx=%s", argFileName, BOOL_CSTRW(argDeleteFile), ctx->str().c_str());

    if (!argDeleteFile)
    {
        // �폜�̃L�����Z���͓���̂Ŗ���

        errorW(L"Can not cancel to delete ctx=%s", ctx->str().c_str());
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    const auto objKey{ ctx->getObjectKey() };

    switch (ctx->getDirEntry()->mFileType)
    {
        case FileTypeEnum::Directory:
        {
            // SetDelete �ł���΃G���[��ԋp�ł���̂ŁA�f�B���N�g���̍폜�͂����Ŏ��{���Ă��܂�

            DirEntryListType dirEntryList;

            if (!mDevice->listObjects(START_CALLER objKey, &dirEntryList))
            {
                errorW(L"fault: listObjects objKey=%s", objKey.c_str());
                return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
            }

            // �f�B���N�g�����폜�ł��邩�`�F�b�N

            switch (mRuntimeEnv->DeleteDirCondition)
            {
                case 1:
                {
                    // �T�u�f�B���N�g��������ꍇ�͍폜�s��

                    const auto it = std::find_if(dirEntryList.cbegin(), dirEntryList.cend(), [](const auto& item)
                    {
                        return FA_IS_DIR(item->mFileInfo.FileAttributes);
                    });

                    if (it != dirEntryList.cend())
                    {
                        traceW(L"dir not empty");
                        return STATUS_CANNOT_DELETE;
                        //return STATUS_DIRECTORY_NOT_EMPTY;
                    }

                    break;
                }

                case 2:
                {
                    // ��̃f�B���N�g���ȊO�͍폜�s��

                    if (!dirEntryList.empty())
                    {
                        traceW(L"dir not empty");
                        return STATUS_CANNOT_DELETE;
                        //return STATUS_DIRECTORY_NOT_EMPTY;
                    }

                    break;
                }
            }

            // �����[�g�̃f�B���N�g�����폜

            const auto& refWinPath{ ctx->getWinPath() };

            while (true)
            {
                //int numDelete = 0;
                std::list<std::wstring> delKeys;

                for (const auto& dirEntry: dirEntryList)
                {
                    APP_ASSERT(dirEntry->mName != L"." && dirEntry->mName != L"..");

                    if (FA_IS_DIR(dirEntry->mFileInfo.FileAttributes))
                    {
                        // �폜�J�n���炱���܂ł̊ԂɃf�B���N�g�����쐬�����\�����l��
                        // ���݂����疳��

                        continue;
                    }

                    // ���[�J���̃L���b�V���E�t�@�C�����폜

                    const auto winPath{ refWinPath / dirEntry->getFileNameBuf() };

                    std::filesystem::path cacheFilePath;

                    if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, winPath, &cacheFilePath))
                    {
                        errorW(L"fault: resolveCacheFilePath winPath=%s", winPath.c_str());
                        return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
                    }

                    traceW(L"delete cacheFilePath=%s", cacheFilePath.c_str());

                    if (!::DeleteFileW(cacheFilePath.c_str()))
                    {
                        const auto lerr = ::GetLastError();
                        if (lerr != ERROR_FILE_NOT_FOUND)
                        {
                            errorW(L"fault: DeleteFileW lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
                            return FspNtStatusFromWin32(lerr);
                        }
                    }

                    const auto delKey{ objKey.append(dirEntry->mName).key() };

                    traceW(L"add del list delKey=%s", delKey.c_str());
                    delKeys.push_back(delKey);
                }

                if (delKeys.empty())
                {
                    break;
                }
                else
                {
                    if (!mDevice->deleteObjects(START_CALLER objKey.bucket(), delKeys))
                    {
                        errorW(L"fault: deleteObjects bucket=%s keys=%s", objKey.c_str(), JoinStrings(delKeys, L',', true).c_str());
                        return STATUS_CANNOT_DELETE;
                    }
                }

                //
                // ��x�� listObjects �ł͍ő吔�̐��������邩������Ȃ��̂ŁA�폜����
                // �Ώۂ��Ȃ��Ȃ�܂ŌJ��Ԃ�
                //

                if (!mDevice->listObjects(START_CALLER objKey, &dirEntryList))
                {
                    errorW(L"fault: listObjects objKey=%s", objKey.c_str());
                    return STATUS_CANNOT_DELETE;
                }
            }

            break;
        }

        case FileTypeEnum::File:
        {
            /*
            https://stackoverflow.com/questions/36217150/deleting-a-file-based-on-disk-id

            SetFileInformationByHandle �� FILE_DISPOSITION_INFO �Ƌ��Ɏg�p����ƁA
            �J���Ă���n���h�������t�@�C�����A���ׂẴn���h��������ꂽ�Ƃ��ɍ폜�����悤�ɐݒ�ł��܂��B
            */

            HANDLE Handle = ctx->getWritableHandle();

            FILE_DISPOSITION_INFO DispositionInfo{};

            DispositionInfo.DeleteFile = argDeleteFile;

            if (!::SetFileInformationByHandle(Handle, FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
            {
                errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
                return FspNtStatusFromWin32(::GetLastError());
            }

            break;
        }
    }

    if (!mDevice->deleteObject(START_CALLER objKey))
    {
        errorW(L"fault: deleteObject objKey=%s", objKey.c_str());
        return STATUS_CANNOT_DELETE;
    }

    return STATUS_SUCCESS;
}

}   // namespace CSEDRV

// EOF