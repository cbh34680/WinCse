#include "AwsS3.hpp"
#include <fstream>
#include <iostream>

using namespace WinCseLib;


bool AwsS3::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const char* sourceFile, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    FSP_FSCTL_FILE_INFO fileInfo{};

    if (sourceFile == nullptr)
    {
        // create() ����Ăяo�����ꍇ�͂������ʉ�
        // --> �܂����[�J���E�L���b�V�����쐬�����O�Ȃ̂ŁA�t�@�C�������Ȃ�

        FILETIME ft;
        ::GetSystemTimeAsFileTime(&ft);

        const auto dirInfo{ makeDirInfo_byName(argObjKey, WinFileTimeToWinFileTime100ns(ft)) };

        fileInfo = dirInfo->FileInfo;
    }
    else
    {
        // ���[�J���E�L���b�V���̓��e���A�b�v���[�h����

        APP_ASSERT(argObjKey.meansFile());

        if (!PathToFileInfoA(sourceFile, &fileInfo))
        {
            traceW(L"fault: PathToFileInfoA");
            return false;
        }

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>
        (
            __FUNCTION__,
            sourceFile,
            std::ios_base::in | std::ios_base::binary
        );

        if (!inputData->good())
        {
            traceW(L"fault: inputData->good");
            return false;
        }

        request.SetBody(inputData);
    }

    request.AddMetadata("wincse-creation-time", std::to_string(fileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-last-access-time", std::to_string(fileInfo.LastAccessTime).c_str());
    request.AddMetadata("wincse-last-write-time", std::to_string(fileInfo.LastWriteTime).c_str());

    request.AddMetadata("wincse-creation-time-debug", WinFileTime100nsToLocalTimeStringA(fileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-last-access-time-debug", WinFileTime100nsToLocalTimeStringA(fileInfo.LastAccessTime).c_str());
    request.AddMetadata("wincse-last-write-time-debug", WinFileTime100nsToLocalTimeStringA(fileInfo.LastWriteTime).c_str());

    const auto outcome = mClient->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    // �L���b�V���E����������폜
    //
    // �㑱�̏����� DoGetSecurityByName() ���Ă΂�邪�A��L�ō쐬�����f�B���N�g����
    // �L���b�V���ɔ��f����Ă��Ȃ���Ԃŗ��p����Ă��܂����Ƃ�������邽�߂�
    // ���O�ɍ폜���Ă����A���߂ăL���b�V�����쐬������

    const auto num = deleteCacheByObjectKey(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d", num);

    if (pFileInfo)
    {
        *pFileInfo = fileInfo;
    }

    return true;
}

CSDeviceContext* AwsS3::create(CALLER_ARG const ObjectKey& argObjKey,
    const UINT32 CreateOptions, const UINT32 GrantedAccess, const UINT32 argFileAttributes,
    PSECURITY_DESCRIPTOR SecurityDescriptor, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    StatsIncr(create);
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.hasKey());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    FSP_FSCTL_FILE_INFO fileInfo{};

    ObjectKey objKey{ CreateOptions & FILE_DIRECTORY_FILE ? std::move(argObjKey.toDir()) : argObjKey };

    if (headObject(CONT_CALLER objKey, &fileInfo))
    {
        // ���݂��Ă���̂ŁAfileInfo �͂��̂܂ܗ��p�ł���

        // --> �f�B���N�g���̏ꍇ�͑��ݗL���Ɋւ�炸�A��ʂ̊K�w���牺�ʂɌ�������
        //     ���x���Ăяo�����̂ŁA���݂���Ƃ��͉������Ȃ�
    }
    else
    {
        // ���݂��Ȃ���΍쐬

        // �����[�g�ɋ�t�@�C����f�B���N�g�����쐬���A���̏�� fileInfo �ɕۑ������

        if (!putObject(CONT_CALLER objKey, nullptr, &fileInfo))
        {
            traceW(L"fault: putObject");
            return nullptr;
        }
    }

    traceW(L"objKey=%s", objKey.c_str());

    const auto remotePath{ objKey.str() };

    // �t�@�C�����ւ̎Q�Ƃ�o�^

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

        HANDLE hFile = INVALID_HANDLE_VALUE;

        if (CreateOptions & FILE_DIRECTORY_FILE)
        {
            // go next
        }
        else
        {
            const auto localPath{ mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(argObjKey.str()) };
            traceW(L"localPath=%s", localPath.c_str());

            UINT32 FileAttributes = argFileAttributes;
            SECURITY_ATTRIBUTES SecurityAttributes{};
            ULONG CreateFlags = 0;

            SecurityAttributes.nLength = sizeof SecurityAttributes;
            SecurityAttributes.lpSecurityDescriptor = SecurityDescriptor;
            SecurityAttributes.bInheritHandle = FALSE;

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
                GrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &SecurityAttributes,
                CREATE_ALWAYS, CreateFlags | FileAttributes, 0);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return nullptr;
            }
        }

        OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, fileInfo, CreateOptions, GrantedAccess);
        APP_ASSERT(ctx);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            // �f�B���N�g���̏ꍇ
        }
        else
        {
            // �t�@�C���̏ꍇ

            ctx->mFile = hFile;
            APP_ASSERT(ctx->mFile.valid());

            // �t�@�C�������𓯊�

            if (!ctx->mFile.setFileTime(fileInfo.CreationTime, fileInfo.LastWriteTime))
            {
                APP_ASSERT(0);

                traceW(L"fault: setLocalTimeTime");
                delete ctx;

                return nullptr;
            }
        }

        *pFileInfo = fileInfo;

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
    // �t�@�C���̃_�E�����[�h���� (�L���b�V���E�t�@�C��) ���s���B

    OpenContext* ctx = new OpenContext(mCacheDataDir, argObjKey, FileInfo, CreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    return ctx;
}

void AwsS3::cleanup(CALLER_ARG WinCseLib::CSDeviceContext* ctx, ULONG Flags)
{
    StatsIncr(cleanup);
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    if (Flags & FspCleanupDelete)
    {
        // setDelete() �ɂ��폜�t���O��ݒ肳�ꂽ�t�@�C���ƁA
        // CreateFile() ���� FILE_FLAG_DELETE_ON_CLOSE �̑������^����ꂽ�t�@�C��
        // ���N���[�Y�����Ƃ��ɂ�����ʉ߂���

        Aws::S3::Model::DeleteObjectRequest request;
        request.SetBucket(ctx->mObjKey.bucketA());
        request.SetKey(ctx->mObjKey.keyA());
        const auto outcome = mClient->DeleteObject(request);

        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: DeleteObject");
        }

        // �L���b�V���E����������폜

        const auto num = deleteCacheByObjectKey(CONT_CALLER ctx->mObjKey);
        traceW(L"cache delete num=%d", num);

        // WinFsp �� Cleanup() �� CloseHandle() ���Ă���̂ŁA���l�̏������s��

        ctx->mFile.close();
    }
}

void AwsS3::close(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);

    traceW(L"close mObjKey=%s path=%s", ctx->mObjKey.c_str(), ctx->getFilePathW().c_str());

    if (ctx->mFile.valid() && ctx->mWrite)
    {
        APP_ASSERT(ctx->mObjKey.meansFile());

        const auto fileSize = ctx->mFile.getFileSize();

        traceW(L"fileSize=%lld", fileSize);

        if (fileSize == 0)
        {
            // nothing
        }
        else
        {
            ctx->mFile.close();

            const auto remotePath{ ctx->getRemotePath() };

            UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
            {
                const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

                if (fileSize < FILESIZE_1GiB * 5)
                {
                    if (!putObject(CONT_CALLER ctx->mObjKey, ctx->getFilePathA().c_str(), nullptr))
                    {
                        traceW(L"fault: putObject");
                    }
                }
                else
                {
                    traceW(L"fault: too big");
                }

            }   // ���O�̃��b�N������ (safeShare �̐�������)
        }
    }

    delete ctx;
}

    // EOF