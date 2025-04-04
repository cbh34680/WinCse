#include "AwsS3.hpp"
#include "AwsS3_obj_pp_util.h"

using namespace WCSE;


static NTSTATUS syncFileAttributes(CALLER_ARG
    const FSP_FSCTL_FILE_INFO& fileInfo, const std::wstring& localPath, bool* pNeedDownload);

NTSTATUS AwsS3::prepareLocalFile_simple(CALLER_ARG OpenContext* ctx, const UINT64 argOffset, const ULONG argLength)
{
    NEW_LOG_BLOCK();

    const auto remotePath{ ctx->mObjKey.str() };

    traceW(L"remotePath=%s", remotePath.c_str());

    // �t�@�C�����ւ̎Q�Ƃ�o�^

    UnprotectedShare<PrepareLocalFileShare> unsafeShare(&mPrepareLocalFileShare, remotePath);    // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() }; // ���O�̃��b�N

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() ��̏���̌Ăяo��

            std::wstring localPath;

            if (!ctx->getCacheFilePath(&localPath))
            {
                //traceW(L"fault: getCacheFilePath");
                //return STATUS_OBJECT_NAME_NOT_FOUND;
                return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
            }

            // �_�E�����[�h���K�v�����f

            bool needDownload = false;

            NTSTATUS ntstatus = syncFileAttributes(CONT_CALLER ctx->mFileInfo, localPath, &needDownload);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: syncFileAttributes");
                return ntstatus;
            }

            //traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

            if (!needDownload)
            {
                if (ctx->mFileInfo.FileSize == 0)
                {
                    // syncFileAttributes() �Ńg�����P�[�g��

                    //return STATUS_END_OF_FILE;
                    return FspNtStatusFromWin32(ERROR_HANDLE_EOF);
                }
            }

            if (ctx->mFileInfo.FileSize <= PART_SIZE_BYTE)
            {
                // ��x�őS�Ă��_�E�����[�h

                if (needDownload)
                {
                    // �L���b�V���E�t�@�C���̏���

                    const FileOutputParams outputParams{ localPath, CREATE_ALWAYS };

                    const auto bytesWritten = this->getObjectAndWriteToFile(CONT_CALLER ctx->mObjKey, outputParams);

                    if (bytesWritten < 0)
                    {
                        traceW(L"fault: getObjectAndWriteToFile_Simple bytesWritten=%lld", bytesWritten);
                        //return STATUS_IO_DEVICE_ERROR;
                        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
                    }
                }

                // �����̃t�@�C�����J��

                ntstatus = ctx->openFileHandle(CONT_CALLER
                    FILE_WRITE_ATTRIBUTES,
                    OPEN_EXISTING
                );

                if (!NT_SUCCESS(ntstatus))
                {
                    traceW(L"fault: openFileHandle");
                    return ntstatus;
                }

                APP_ASSERT(ctx->mFile.valid());
            }
            else
            {
                // �}���`�p�[�g�E�_�E�����[�h

                // �t�@�C�����J��

                ntstatus = ctx->openFileHandle(CONT_CALLER
                    FILE_WRITE_ATTRIBUTES,
                    needDownload ? CREATE_ALWAYS : OPEN_EXISTING
                );

                if (!NT_SUCCESS(ntstatus))
                {
                    traceW(L"fault: openFileHandle");
                    return ntstatus;
                }

                APP_ASSERT(ctx->mFile.valid());

                if (needDownload)
                {
                    // �_�E�����[�h���K�v

                    if (!this->doMultipartDownload(CONT_CALLER ctx, localPath))
                    {
                        traceW(L"fault: doMultipartDownload");
                        //return STATUS_IO_DEVICE_ERROR;
                        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
                    }
                }
            }

            if (needDownload)
            {
                // �t�@�C�����t�̓���

                if (!ctx->mFile.setFileTime(ctx->mFileInfo))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setBasicInfo lerr=%lu", lerr);

                    return FspNtStatusFromWin32(lerr);
                }
            }
            else
            {
                // �A�N�Z�X�����̂ݍX�V

                if (!ctx->mFile.setFileTime(0, 0))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setFileTime lerr=%lu", lerr);

                    return FspNtStatusFromWin32(lerr);
                }
            }

            // �������̃T�C�Y�Ɣ�r

            LARGE_INTEGER fileSize;
            if(!::GetFileSizeEx(ctx->mFile.handle(), &fileSize))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

                return FspNtStatusFromWin32(lerr);
            }

            if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
            {
                APP_ASSERT(0);

                traceW(L"fault: no match filesize ");
                //return STATUS_IO_DEVICE_ERROR;
                return FspNtStatusFromWin32(ERROR_IO_DEVICE);
            }
        }
    }   // ���O�̃��b�N������ (safeShare �̐�������)

    APP_ASSERT(ctx->mFile.valid());

    return STATUS_SUCCESS;
}

static NTSTATUS syncFileAttributes(CALLER_ARG const FSP_FSCTL_FILE_INFO& remoteInfo,
    const std::wstring& localPath, bool* pNeedDownload)
{
    //
    // �����[�g�̃t�@�C�����������[�J���̃L���b�V���E�t�@�C���ɔ��f����
    // �_�E�����[�h���K�v�ȏꍇ�� pNeedDownload �ɂ��ʒm
    //
    NEW_LOG_BLOCK();
    APP_ASSERT(pNeedDownload);

    //traceW(L"argObjKey=%s localPath=%s", argObjKey.c_str(), localPath.c_str());
    //traceW(L"remoteInfo FileSize=%llu LastWriteTime=%llu", remoteInfo.FileSize, remoteInfo.LastWriteTime);
    //traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", remoteInfo.CreationTime, remoteInfo.LastWriteTime);

    FSP_FSCTL_FILE_INFO localInfo{};

    // 
    // * �p�^�[��
    //      �S�ē����ꍇ�͉������Ȃ�
    //      �قȂ��Ă�����̂�����ꍇ�͈ȉ��̕\�ɏ]��
    // 
    //                                      +-----------------------------------------+
    //				                        | �����[�g                                |
    //                                      +---------------------+-------------------+
    //				                        | �T�C�Y==0	          | �T�C�Y>0          |
    // ------------+------------+-----------+---------------------+-------------------+
    //	���[�J��   | ���݂���   | �T�C�Y==0 | �X�V�����𓯊�      | �_�E�����[�h      |
    //             |            +-----------+---------------------+-------------------+
    //			   |            | �T�C�Y>0  | �؂�l��            | �_�E�����[�h      |
    //             +------------+-----------+---------------------+-------------------+
    //		       | ���݂��Ȃ�	|	        | ��t�@�C���쐬      | �_�E�����[�h      |
    // ------------+------------+-----------+---------------------+-------------------+
    //
    bool syncTime = false;
    bool truncateFile = false;
    bool needDownload = false;
    DWORD lastError = ERROR_SUCCESS;
    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    FileHandle hFile = ::CreateFileW
    (
        localPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    lastError = ::GetLastError();

    if (hFile.valid())
    {
        //traceW(L"exists: local");

        // ���[�J���E�t�@�C�������݂���

        ntstatus = GetFileInfoInternal(hFile.handle(), &localInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return ntstatus;
        }

        //traceW(L"localInfo FileSize=%llu LastWriteTime=%llu", localInfo.FileSize, localInfo.LastWriteTime);
        //traceW(L"localInfo CreationTime=%llu LastWriteTime=%llu", localInfo.CreationTime, localInfo.LastWriteTime);

        if (remoteInfo.FileSize == localInfo.FileSize &&
            localInfo.CreationTime == remoteInfo.CreationTime &&
            localInfo.LastWriteTime == remoteInfo.LastWriteTime)
        {
            // --> �S�ē����Ȃ̂ŏ����s�v

            traceW(L"same file, skip, localPath=%s", localPath.c_str());
        }
        else
        {
            if (remoteInfo.FileSize == 0)
            {
                if (localInfo.FileSize == 0)
                {
                    // ���[�J�� == 0 : �����[�g == 0
                    // --> �X�V�����𓯊�

                    syncTime = true;
                }
                else
                {
                    // ���[�J�� > 0 : �����[�g == 0
                    // --> �؂�l��

                    truncateFile = true;
                }
            }
            else
            {
                // �����[�g > 0
                // --> �_�E�����[�h

                needDownload = true;
            }
        }
    }
    else
    {
        if (lastError != ERROR_FILE_NOT_FOUND)
        {
            // �z�肵�Ȃ��G���[

            traceW(L"fault: CreateFileW lerr=%lu", lastError);
            return FspNtStatusFromWin32(lastError);
        }

        //traceW(L"not exists: local");

        // ���[�J���E�t�@�C�������݂��Ȃ�

        if (remoteInfo.FileSize == 0)
        {
            // --> ��t�@�C���쐬

            truncateFile = true;
        }
        else
        {
            // --> �_�E�����[�h

            needDownload = true;
        }
    }

    traceW(L"syncRemoteTime=%s, truncateLocal=%s, needDownload=%s",
        BOOL_CSTRW(syncTime), BOOL_CSTRW(truncateFile), BOOL_CSTRW(needDownload));

    if (syncTime && truncateFile)
    {
        APP_ASSERT(0);
    }

    if (syncTime || truncateFile)
    {
        APP_ASSERT(!needDownload);

        hFile = ::CreateFileW
        (
            localPath.c_str(),
            FILE_WRITE_ATTRIBUTES, //GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            truncateFile ? CREATE_ALWAYS : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile.invalid())
        {
            lastError = ::GetLastError();
            traceW(L"fault: CreateFileW lerr=%lu", lastError);

            return FspNtStatusFromWin32(lastError);
        }

        // �X�V�����𓯊�

        //traceW(L"setFileTime");

        if (!hFile.setFileTime(remoteInfo.CreationTime, remoteInfo.LastWriteTime))
        {
            lastError = ::GetLastError();
            traceW(L"fault: setFileTime lerr=%lu", lastError);

            return FspNtStatusFromWin32(lastError);
        }

        ntstatus = GetFileInfoInternal(hFile.handle(), &localInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return ntstatus;
        }

        if (truncateFile)
        {
            traceW(L"truncate localPath=%s", localPath.c_str());
        }
        else
        {
            traceW(L"sync localPath=%s", localPath.c_str());
        }
    }

    if (!needDownload)
    {
        // �_�E�����[�h���s�v�ȏꍇ�́A���[�J���Ƀt�@�C�������݂����ԂɂȂ��Ă���͂�

        APP_ASSERT(hFile.valid());
        APP_ASSERT(localInfo.CreationTime);
    }

    *pNeedDownload = needDownload;

    return STATUS_SUCCESS;
}

// EOF