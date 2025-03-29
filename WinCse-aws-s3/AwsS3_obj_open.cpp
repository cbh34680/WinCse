#include "AwsS3.hpp"
#include <fstream>
#include <iostream>

using namespace WinCseLib;


CSDeviceContext* AwsS3::create(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& fileInfo, const UINT32 CreateOptions,
    const UINT32 GrantedAccess, const UINT32 argFileAttributes)
{
    StatsIncr(create);
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s", argObjKey.c_str());

    const auto remotePath{ argObjKey.str() };

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

        FileHandle hFile;

        if (CreateOptions & FILE_DIRECTORY_FILE)
        {
            // go next
        }
        else
        {
            std::wstring localPath;

            if (!GetCacheFilePath(mCacheDataDir, argObjKey.str(), &localPath))
            {
                return nullptr;
            }

            traceW(L"localPath=%s", localPath.c_str());

            UINT32 FileAttributes = argFileAttributes;
            ULONG CreateFlags = 0;
            //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // �f�B���N�g���͑��삵�Ȃ��̂ŕs�v

            if (CreateOptions & FILE_DELETE_ON_CLOSE)
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

            //if (CreateOptions & FILE_DIRECTORY_FILE)
            //{
                /*
                * It is not widely known but CreateFileW can be used to create directories!
                * It requires the specification of both FILE_FLAG_BACKUP_SEMANTICS and
                * FILE_FLAG_POSIX_SEMANTICS. It also requires that FileAttributes has
                * FILE_ATTRIBUTE_DIRECTORY set.
                */
                //CreateFlags |= FILE_FLAG_POSIX_SEMANTICS;         // �f�B���N�g���͑��삵�Ȃ��̂ŕs�v
                //FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;       // �f�B���N�g���͑��삵�Ȃ��̂ŕs�v
            //}
            //else
                FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

            if (0 == FileAttributes)
                FileAttributes = FILE_ATTRIBUTE_NORMAL;

            hFile = ::CreateFileW(localPath.c_str(),
                GrantedAccess,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                CREATE_ALWAYS,
                CreateFlags | FileAttributes,
                NULL);

            if (hFile.invalid())
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return nullptr;
            }

            // �t�@�C�������𓯊�

            if (!hFile.setFileTime(fileInfo))
            {
                traceW(L"fault: setLocalTimeTime");

                return nullptr;
            }
        }

        OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, fileInfo, CreateOptions, GrantedAccess);
        APP_ASSERT(ctx);

        if (hFile.valid())
        {
            // �t�@�C���̏ꍇ

            ctx->mFile = std::move(hFile);

            APP_ASSERT(ctx->mFile.valid());
            APP_ASSERT(hFile.invalid());
        }

        return ctx;

    }   // ���O�̃��b�N������ (safeShare �̐�������)
}

CSDeviceContext* AwsS3::open(CALLER_ARG const ObjectKey& argObjKey,
    const UINT32 CreateOptions, const UINT32 GrantedAccess,
    const FSP_FSCTL_FILE_INFO& FileInfo)
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

void AwsS3::close(CALLER_ARG WinCseLib::CSDeviceContext* ctx)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    traceW(L"close mObjKey=%s", ctx->mObjKey.c_str());

    if (ctx->mFile.valid() && ctx->mFlags & CSDCTX_FLAGS_MODIFY)
    {
        // cleanup() �ō폜�����t�@�C���̓N���[�Y����Ă���̂�
        // ������ʉ߂���̂̓A�b�v���[�h����K�v�̂���t�@�C���݂̂ƂȂ��Ă���͂�

        APP_ASSERT(ctx->isFile());
        APP_ASSERT(ctx->mObjKey.meansFile());

        const auto fileSize = ctx->mFile.getFileSize();

        traceW(L"fileSize=%lld", fileSize);

        // ���Ă����Ȃ��� putObject() �ɂ��� Aws::FStream �����s����

        ctx->mFile.close();

        const auto remotePath{ ctx->mObjKey.str() };

        UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
        {
            const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

            if (fileSize < FILESIZE_1GiB * 5)
            {
                std::wstring localPath;

                if (ctx->getCacheFilePath(&localPath))
                {
                    if (!putObject(CONT_CALLER ctx->mObjKey, localPath.c_str(), nullptr))
                    {
                        traceW(L"fault: putObject");
                    }

                    headObject(CONT_CALLER ctx->mObjKey, nullptr);
                }
                else
                {
                    traceW(L"fault: getCacheFilePath");
                }
            }
            else
            {
                traceW(L"fault: too big");
            }

        }   // ���O�̃��b�N������ (safeShare �̐�������)
    }

    delete ctx;
}

    // EOF