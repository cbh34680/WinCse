#include "AwsS3.hpp"
#include <fstream>
#include <iostream>

using namespace WCSE;


CSDeviceContext* AwsS3::create(CALLER_ARG const ObjectKey& argObjKey,
    UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes)
{
    StatsIncr(create);
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s", argObjKey.c_str());

    UINT32 GrantedAccess = argGrantedAccess;
    UINT32 FileAttributes = argFileAttributes;
    const bool isDirectory = argCreateOptions & FILE_DIRECTORY_FILE;

    const auto remotePath{ argObjKey.str() };
    FileHandle hFile;

    UnprotectedShare<PrepareLocalFileShare> unsafeShare(&mPrepareLocalFileShare, remotePath);   // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        const auto localPath{ GetCacheFilePath(mCacheDataDir, argObjKey.str()) };

        traceW(L"localPath=%s", localPath.c_str());

        ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;

        if (argCreateOptions & FILE_DELETE_ON_CLOSE)
        {
            CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
        }

        if (isDirectory)
        {
            /*
            * It is not widely known but CreateFileW can be used to create directories!
            * It requires the specification of both FILE_FLAG_BACKUP_SEMANTICS and
            * FILE_FLAG_POSIX_SEMANTICS. It also requires that FileAttributes has
            * FILE_ATTRIBUTE_DIRECTORY set.
            */
            CreateFlags |= FILE_FLAG_POSIX_SEMANTICS;
            FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

            // �f�B���N�g�����쐬����ꍇ�́A��L�ɉ����� CREATE_NEW �ł���K�v������

            // �Ȃ̂ŁA�\�ߍ폜����

            if (!::RemoveDirectoryW(localPath.c_str()))
            {
                const auto lerr = ::GetLastError();
                if (lerr != ERROR_FILE_NOT_FOUND)
                {
                    traceW(L"fault: RemoveDirectory, lerr=%lu", lerr);
                    return nullptr;
                }
            }
        }
        else
        {
            FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;
        }

        if (0 == FileAttributes)
        {
            FileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

        hFile = ::CreateFileW(localPath.c_str(),
            GrantedAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            isDirectory ? CREATE_NEW : CREATE_ALWAYS,
            CreateFlags | FileAttributes,
            NULL);

    }   // ���O�̃��b�N������ (safeShare �̐�������)

    if (hFile.invalid())
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%lu", lerr);

        return nullptr;
    }

    FSP_FSCTL_FILE_INFO fileInfo;
    const auto ntstatus = GetFileInfoInternal(hFile.handle(), &fileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return nullptr;
    }

    OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, fileInfo, argCreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    if (isDirectory)
    {
        // �f�B���N�g���̏ꍇ�� ctx �ɕۑ�����K�v���Ȃ��̂� ���̂܂ܕ���
    }
    else
    {
        // �t�@�C���̏ꍇ

        ctx->mFile = std::move(hFile);

        // move �̊m�F

        APP_ASSERT(ctx->mFile.valid());
        APP_ASSERT(hFile.invalid());
    }

    // �L���b�V���̍폜
    // 
    // --> �e�f�B���N�g���̃L���b�V�����폜���Ă����Ȃ��ƁA�V�K�쐬�������̂�
    //     ���f����Ȃ���ԂɂȂ��Ă��܂�

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return ctx;
}

CSDeviceContext* AwsS3::open(CALLER_ARG const ObjectKey& argObjKey,
    UINT32 CreateOptions, UINT32 GrantedAccess, const FSP_FSCTL_FILE_INFO& FileInfo)
{
    StatsIncr(open);
    NEW_LOG_BLOCK();

    // DoOpen() ����Ăяo����邪�A�t�@�C�����J��=�_�E�����[�h�ɂȂ��Ă��܂�����
    // �����ł� UParam �ɏ��݂̂�ۑ����ADoRead() ����Ăяo����� readFile() ��
    // �t�@�C���̃_�E�����[�h���� (�L���b�V���E�t�@�C���̍쐬) ���s���B

    OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, FileInfo, CreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    return ctx;
}

bool AwsS3::uploadWhenClosing(CALLER_ARG WCSE::CSDeviceContext* ctx, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();

    FSP_FSCTL_FILE_INFO fileInfo{};

    if (ctx->isDir())
    {
        // �f�B���N�g���̏ꍇ�� create �̂Ƃ��̏������̂܂ܓ]�L

        traceW(L"directory");

        fileInfo = ctx->mFileInfo;
    }
    else if (ctx->isFile())
    {
        APP_ASSERT(ctx->mFile.valid());
        APP_ASSERT(ctx->mObjKey.meansFile());

        traceW(L"valid file");

        // ���������擾���邽�߁A�t�@�C�������O�� flush ����

        if (!::FlushFileBuffers(ctx->mFile.handle()))
        {
            const auto lerr = ::GetLastError();

            traceW(L"fault: FlushFileBuffers, lerr=%lu", lerr);
            return false;
        }

        const auto ntstatus = GetFileInfoInternal(ctx->mFile.handle(), &fileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return false;
        }

        // ���Ă����Ȃ��� putObject() �ɂ��� Aws::FStream �����s����

        ctx->mFile.close();
    }

    // �A�b�v���[�h

    traceW(L"putObject mObjKey=%s, localPath=%s", ctx->mObjKey.c_str(), localPath.c_str());

    if (!this->putObject(CONT_CALLER ctx->mObjKey, fileInfo, localPath))
    {
        traceW(L"fault: putObject");
        return false;
    }

    return true;
}

static std::wstring CsdCtxFlagsStr(uint32_t flags)
{
    std::list<std::wstring> strs;

    if (flags & CSDCTX_FLAGS_MODIFY)            strs.emplace_back(L"modify");
    if (flags & CSDCTX_FLAGS_READ)              strs.emplace_back(L"read");
    if (flags & CSDCTX_FLAGS_M_CREATE)          strs.emplace_back(L"create");
    if (flags & CSDCTX_FLAGS_M_WRITE)           strs.emplace_back(L"write");
    if (flags & CSDCTX_FLAGS_M_OVERWRITE)       strs.emplace_back(L"overwrite");
    if (flags & CSDCTX_FLAGS_M_SET_BASIC_INFO)  strs.emplace_back(L"set_basic_info");
    if (flags & CSDCTX_FLAGS_M_SET_FILE_SIZE)   strs.emplace_back(L"set_file_size");

    return JoinStrings(strs, L", ", false);
}

void AwsS3::close(CALLER_ARG WCSE::CSDeviceContext* ctx)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    //traceW(L"close mObjKey=%s", ctx->mObjKey.c_str());

    if (!(ctx->mFlags & CSDCTX_FLAGS_MODIFY))
    {
        return;
    }

    traceW(L"mFlage=%s", CsdCtxFlagsStr(ctx->mFlags).c_str());

    // �L���b�V���E�t�@�C����

    const auto localPath{ ctx->getCacheFilePath() };

    traceW(L"uploadWhenClosing mObjKey=%s, localPath=%s", ctx->mObjKey.c_str(), localPath.c_str());

    // �t�@�C���E�A�b�v���[�h

    if (!this->uploadWhenClosing(CONT_CALLER ctx, localPath))
    {
        traceW(L"fault: uploadWhenClosing");
        return;
    }

    // (config �̐ݒ�ɂ��)�A�b�v���[�h�����t�@�C�����폜����

    if (mSettings->deleteAfterUpload)
    {
        if (ctx->isDir())
        {
            // ������ʉ߂���̂͐V�K�쐬���݂̂ł���A��̃f�B���N�g����
            // ���l�[���ۂ̔��f�ޗ��ƂȂ邽�ߍ폜���Ȃ�
        }
        else if (ctx->isFile())
        {
            // �t�@�C�����폜

            traceW(L"delete local cache: %s", localPath.c_str());

            if (!::DeleteFileW(localPath.c_str()))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: DeleteFileW, lerr=%lu", lerr);
            }

            //traceW(L"success");
        }
    }
}

    // EOF