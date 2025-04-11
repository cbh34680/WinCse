#include "AwsS3.hpp"
#include <fstream>

using namespace WCSE;


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
NTSTATUS AwsS3::readObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    StatsIncr(readObject);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s, ctx=%p HANDLE=%p, Offset=%llu, Length=%lu",
        ctx->mObjKey.c_str(), ctx, ctx->mFile.handle(), Offset, Length);

    const auto ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: prepareLocalFile");
        return ntstatus;
    }

    APP_ASSERT(ctx->mFile.valid());

    // Offset, Length �ɂ��t�@�C����ǂ�

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: ReadFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"PBytesTransferred=%lu", *PBytesTransferred);

    return STATUS_SUCCESS;
}

NTSTATUS AwsS3::writeObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s, ctx=%p HANDLE=%p, Offset=%llu, Length=%lu, WriteToEndOfFile=%s, ConstrainedIo=%s",
        ctx->mObjKey.c_str(), ctx, ctx->mFile.handle(), Offset, Length,
        BOOL_CSTRW(WriteToEndOfFile), BOOL_CSTRW(ConstrainedIo));

    auto ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: prepareLocalFile");
        return ntstatus;
    }

    APP_ASSERT(ctx->mFile.valid());

    if (ConstrainedIo)
    {
        LARGE_INTEGER FileSize;

        if (!::GetFileSizeEx(ctx->mFile.handle(), &FileSize))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

            return FspNtStatusFromWin32(lerr);
        }

        if (Offset >= (UINT64)FileSize.QuadPart)
        {
            return STATUS_SUCCESS;
        }

        if (Offset + Length > (UINT64)FileSize.QuadPart)
        {
            Length = (ULONG)((UINT64)FileSize.QuadPart - Offset);
        }
    }

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::WriteFile(ctx->mFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: WriteFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"PBytesTransferred=%lu", *PBytesTransferred);

    ctx->mFlags |= CSDCTX_FLAGS_WRITE;

    ntstatus = GetFileInfoInternal(ctx->mFile.handle(), FileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return FspNtStatusFromWin32(::GetLastError());
    }

    return STATUS_SUCCESS;
}

bool AwsS3::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    // ��Ƀf�B���N�g�����̃t�@�C������폜����
    // --> �T�u�f�B���N�g���͊܂܂�Ă��Ȃ��͂�

    if (argObjKey.meansDir())
    {
        while (1)
        {
            //
            // ��x�� listObjects �ł͍ő吔�̐��������邩������Ȃ��̂ŁA�폜����
            // �Ώۂ��Ȃ��Ȃ�܂ŌJ��Ԃ�
            //

            DirInfoListType dirInfoList;

            if (!this->listObjects(CONT_CALLER argObjKey, &dirInfoList))
            {
                traceW(L"fault: listObjects");
                return false;
            }

            std::list<std::wstring> delete_objects;

            for (const auto& dirInfo: dirInfoList)
            {
                if (wcscmp(dirInfo->FileNameBuf, L".") == 0 || wcscmp(dirInfo->FileNameBuf, L"..") == 0)
                {
                    continue;
                }

                if (FA_IS_DIRECTORY(dirInfo->FileInfo.FileAttributes))
                {
                    // �폜�J�n���炱���܂ł̊ԂɃf�B���N�g�����쐬�����\�����l��
                    // ���݂����疳��

                    continue;
                }

                const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };
                delete_objects.push_back(fileObjKey.key());

                traceW(L"delete_objects.AddObjects fileObjKey=%s", fileObjKey.c_str());

                // ���[�J���̃L���b�V���E�t�@�C�����폜

                const auto localPath{ GetCacheFilePath(mCacheDataDir, fileObjKey.str()) };

                if (::DeleteFileW(localPath.c_str()))
                {
                    traceW(L"success DeleteFileW localPath=%s", localPath.c_str());
                }
                else
                {
                    const auto lerr = ::GetLastError();
                    if (lerr != ERROR_FILE_NOT_FOUND)
                    {
                        traceW(L"fault: DeleteFileW, lerr=%lu", lerr);
                        return false;
                    }
                }

                // �L���b�V���E����������폜 (�t�@�C��)

                const auto num = deleteObjectCache(CONT_CALLER fileObjKey);
                traceW(L"cache delete num=%d, fileObjKey=%s", num, fileObjKey.c_str());
            }

            if (delete_objects.empty())
            {
                break;
            }

            traceW(L"DeleteObjects bucket=%s size=%zu", argObjKey.bucket().c_str(), delete_objects.size());

            if (!this->apicallDeleteObjects(CONT_CALLER argObjKey.bucket(), delete_objects))
            {
                traceW(L"fault: apicallDeleteObjects");
                return false;
            }
        }
    }

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    if (!this->apicallDeleteObject(CONT_CALLER argObjKey))
    {
        traceW(L"fault: apicallDeleteObject");
        return false;
    }

    if (argObjKey.meansDir())
    {
        // �V�K�쐬���ɍ��ꂽ���[�J���E�f�B���N�g�������݂�����폜

        const auto localPath{ GetCacheFilePath(mCacheDataDir, argObjKey.str()) };

        if (::RemoveDirectoryW(localPath.c_str()))
        {
            traceW(L"success RemoveDirectoryW localPath=%s", localPath.c_str());
        }
        else
        {
            const auto lerr = ::GetLastError();
            if (lerr != ERROR_FILE_NOT_FOUND)
            {
                traceW(L"fault: RemoveDirectoryW, lerr=%lu", lerr);
                //return false;
            }
        }
    }

    // �L���b�V���E����������폜 (�f�B���N�g��)

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool AwsS3::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, const std::wstring& argFilePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argFilePath=%s", argObjKey.c_str(), argFilePath.c_str());

    traceW(L"do apicallPutObject");

    if (!this->apicallPutObject(CONT_CALLER argObjKey, argFileInfo, argFilePath))
    {
        traceW(L"fault: apicallPutObject");
        return false;
    }

    // �L���b�V���E����������폜
    //
    // ��L�ō쐬�����f�B���N�g�����L���b�V���ɔ��f����Ă��Ȃ���Ԃ�
    // ���p����Ă��܂����Ƃ�������邽�߂Ɏ��O�ɍ폜���Ă����A���߂ăL���b�V�����쐬������

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    // headObject() �͕K�{�ł͂Ȃ����A�쐬����ɑ������Q�Ƃ���邱�ƂɑΉ�

    if (!headObject(CONT_CALLER argObjKey, nullptr))
    {
        traceW(L"fault: headObject");
        return false;
    }

    return true;
}

bool AwsS3::renameObject(CALLER_ARG WCSE::CSDeviceContext* ctx, const ObjectKey& argNewObjKey)
{
    NEW_LOG_BLOCK();

    const auto localPath{ ctx->getCacheFilePath() };

    FSP_FSCTL_FILE_INFO localInfo;
    const auto ntstatus = PathToFileInfo(localPath, &localInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: PathToFileInfo, localPath=%s", localPath.c_str());
        return false;
    }

    FSP_FSCTL_FILE_INFO remoteInfo{};

    if (ctx->isFile())
    {
        // �t�@�C���̏ꍇ�� headObject() �̏�񂪐������̂ŁActx �̂��̂����̂܂ܗ��p

        remoteInfo = ctx->mFileInfo;
    }
    else
    {
        // �f�B���N�g���̏ꍇ�A�e�f�B���N�g���� CommonPrefix �����ɂ������ɂȂ�̂�
        // HeadObject �ɂ�胊���[�g�̏����擾����

        DirInfoType dirInfo;

        if (!this->apicallHeadObject(CONT_CALLER ctx->mObjKey, &dirInfo))
        {
            traceW(L"fault: apicallHeadObject");
            return false;
        }

        remoteInfo = dirInfo->FileInfo;
    }

    APP_ASSERT(remoteInfo.CreationTime);

    if (localInfo.CreationTime == remoteInfo.CreationTime &&
        localInfo.LastWriteTime == remoteInfo.LastWriteTime &&
        localInfo.FileSize == remoteInfo.FileSize)
    {
        // go next
    }
    else
    {
        traceW(L"no match local:remote");
        traceW(L"localPath=%s", localPath.c_str());
        traceW(L"mObjKey=%s", ctx->mObjKey.c_str());
        traceW(L"localInfo:  CreationTime=%llu, LastWriteTime=%llu, FileSize=%llu", localInfo.CreationTime, localInfo.LastWriteTime, localInfo.FileSize);
        traceW(L"remoteInfo: CreationTime=%llu, LastWriteTime=%llu, FileSize=%llu", remoteInfo.CreationTime, remoteInfo.LastWriteTime, remoteInfo.FileSize);

        // ��v���Ȃ����[�J���E�L���b�V���͍폜����

        if (ctx->isDir())
        {
            if (::RemoveDirectoryW(localPath.c_str()))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: RemoveDirectory, lerr=%lu", lerr);

                // �G���[�͖���
            }
        }
        else
        {
            if (::DeleteFileW(localPath.c_str()))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: DeleteFileW, lerr=%lu", lerr);

                // �G���[�͖���
            }
        }

        return false;
    }

    // ���[�J���Ƀt�@�C�������݂��A�����[�g�Ɗ��S�Ɉ�v����󋵂Ȃ̂ŁA���l�[�����������{

    // �V�������O�ŃA�b�v���[�h����

    traceW(L"putObject argNewObjKey=%s, localPath=%s", argNewObjKey.c_str(), localPath.c_str());

    if (!this->putObject(CONT_CALLER argNewObjKey, remoteInfo, localPath))
    {
        traceW(L"fault: putObject");

        return false;
    }

    // �Â����O���폜

    if (!this->deleteObject(CONT_CALLER ctx->mObjKey))
    {
        traceW(L"fault: deleteObject");

        return false;
    }

    if (ctx->isDir())
    {
        // �f�B���N�g�����̕ύX�́A�V�K�쐬���̂�
        // --> �폜���邱�ƂŁA�����̃f�B���N�g���̃��l�[���͊֐��擪�ɂ��� PathToFileInfo �Ŏ��s������

        traceW(L"RemoveDirectory localPath=%s", localPath.c_str());

        if (::RemoveDirectoryW(localPath.c_str()))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: RemoveDirectory, lerr=%lu", lerr);

            // �G���[�͖���
        }
    }
    else
    {
        // �L���b�V���E�t�@�C���̃��l�[��

        const auto newLocalPath{ GetCacheFilePath(ctx->mCacheDataDir, argNewObjKey.str()) };

        traceW(L"MoveFileExW localPath=%s, newLocalPath=%s", localPath.c_str(), newLocalPath.c_str());

        if (!::MoveFileExW(localPath.c_str(), newLocalPath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: MoveFileExW, lerr=%lu", lerr);

            // �G���[�͖���
        }
    }

    // �L���b�V���E����������폜

    const auto num = this->deleteObjectCache(CONT_CALLER argNewObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argNewObjKey.c_str());

    // headObject() �͕K�{�ł͂Ȃ����A�쐬����ɑ������Q�Ƃ���邱�ƂɑΉ�

    if (!headObject(CONT_CALLER argNewObjKey, nullptr))
    {
        traceW(L"fault: headObject");

        // �G���[�͖���
    }

    return true;
}

// EOF