#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
NTSTATUS AwsS3::readObject_Simple(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    const auto remotePath{ ctx->getRemotePath() };

    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
        ctx, ctx->mFile.handle(), Offset, Length, remotePath.c_str());

    // �t�@�C�����ւ̎Q�Ƃ�o�^

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() ��̏���̌Ăяo��

            traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                ctx->mFile.handle(), Offset, Length, remotePath.c_str());

            std::wstring localPath;

            if (!ctx->getFilePathW(&localPath))
            {
                traceW(L"fault: getFilePathW");
                return STATUS_OBJECT_PATH_NOT_FOUND;
            }

            // �_�E�����[�h���K�v�����f

            bool needDownload = false;

            if (!syncFileAttributes(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
            {
                traceW(L"fault: syncFileAttributes");
                return STATUS_IO_DEVICE_ERROR;
            }

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

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

                const auto bytesWritten = this->prepareLocalCacheFile(CONT_CALLER ctx->mObjKey, outputParams);

                if (bytesWritten < 0)
                {
                    traceW(L"fault: prepareLocalCacheFile_Simple bytesWritten=%lld", bytesWritten);
                    return STATUS_IO_DEVICE_ERROR;
                }
            }
            else
            {
                if (ctx->mFileInfo.FileSize == 0)
                {
                    return STATUS_END_OF_FILE;
                }
            }

            // �����̃t�@�C�����J��

            NTSTATUS ntstatus = ctx->openFileHandle(CONT_CALLER
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
                // �t�@�C�������𓯊�

                if (!ctx->mFile.setFileTime(ctx->mFileInfo.CreationTime, ctx->mFileInfo.LastWriteTime))
                {
                    traceW(L"fault: setLocalTimeTime");
                    return STATUS_IO_DEVICE_ERROR;
                }
            }
            else
            {
                // �A�N�Z�X�����̂ݍX�V

                if (!ctx->mFile.setFileTime(0, 0))
                {
                    traceW(L"fault: setLocalTimeTime");
                    return STATUS_IO_DEVICE_ERROR;
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
                return STATUS_IO_DEVICE_ERROR;
            }
        }
    }   // ���O�̃��b�N������ (safeShare �̐�������)

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

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mFile.handle(), Offset, Length, *PBytesTransferred);

    return STATUS_SUCCESS;
}

// EOF