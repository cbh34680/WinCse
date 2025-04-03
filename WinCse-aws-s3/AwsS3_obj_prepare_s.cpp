#include "AwsS3.hpp"

using namespace WinCseLib;


NTSTATUS AwsS3::prepareLocalFile_Simple(CALLER_ARG OpenContext* ctx)
{
    NEW_LOG_BLOCK();

    //traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    const auto remotePath{ ctx->mObjKey.str() };

    //traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx, ctx->mFile.handle(), Offset, Length, remotePath.c_str());

    // �t�@�C�����ւ̎Q�Ƃ�o�^

    UnprotectedShare<PrepareLocalFileShare> unsafeShare(&mPrepareLocalFileShare, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() ��̏���̌Ăяo��

            //traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx->mFile.handle(), Offset, Length, remotePath.c_str());

            std::wstring localPath;

            if (!ctx->getCacheFilePath(&localPath))
            {
                traceW(L"fault: getCacheFilePath");
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

            if (needDownload)
            {
                // �L���b�V���E�t�@�C���̏���

                const FileOutputParams outputParams
                {
                    localPath,
                    CREATE_ALWAYS,
                    false,              // SetRange()
                    0,                  // Offset
                    0                   // Length
                };

                const auto bytesWritten = this->getObjectAndWriteToFile(CONT_CALLER ctx->mObjKey, outputParams);

                if (bytesWritten < 0)
                {
                    traceW(L"fault: getObjectAndWriteToFile_Simple bytesWritten=%lld", bytesWritten);
                    //return STATUS_IO_DEVICE_ERROR;
                    return FspNtStatusFromWin32(ERROR_IO_DEVICE);
                }
            }
            else
            {
                if (ctx->mFileInfo.FileSize == 0)
                {
                    // syncFileAttributes() �Ńg�����P�[�g��

                    //return STATUS_END_OF_FILE;
                    return FspNtStatusFromWin32(ERROR_HANDLE_EOF);
                }
            }

            // �����̃t�@�C�����J��

            ntstatus = ctx->openFileHandle(CONT_CALLER
                //needDownload ? FILE_WRITE_ATTRIBUTES : 0,
                FILE_WRITE_ATTRIBUTES,
                OPEN_EXISTING
            );

            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: openFileHandle");
                return ntstatus;
            }

            APP_ASSERT(ctx->mFile.valid());

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

// EOF