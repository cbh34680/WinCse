#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
NTSTATUS AwsS3::readObject_Simple(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);
    APP_ASSERT(ctx->isFile());

    NEW_LOG_BLOCK();

    NTSTATUS ntstatus = STATUS_IO_DEVICE_ERROR;
    OVERLAPPED Overlapped{};

    const auto remotePath{ ctx->getRemotePath() };
    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx, ctx->mLocalFile, Offset, Length, remotePath.c_str());

    {
        // �t�@�C�����ւ̎Q�Ƃ�o�^

        UnprotectedNamedData<Shared_Simple> unsafeShare(remotePath);
        
        {
            // �t�@�C�����̃��b�N
            //
            // �����X���b�h���瓯��t�@�C���ւ̓����A�N�Z�X�͍s���Ȃ�
            // --> �t�@�C�������S�ɑ���ł��邱�Ƃ�ۏ�

            ProtectedNamedData<Shared_Simple> safeShare(unsafeShare);

            //
            // �֐��擪�ł� mLocalFile �̃`�F�b�N�����Ă��邪�A���b�N�L���ŏ󋵂�
            // �ς���Ă��邽�߁A���߂ă`�F�b�N����
            //
            if (ctx->mLocalFile == INVALID_HANDLE_VALUE)
            {
                traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                    ctx->mLocalFile, Offset, Length, remotePath.c_str());

                // openFile() ��̏���̌Ăяo��

                const std::wstring localPath{ ctx->getLocalPath() };

                if (ctx->mFileInfo.FileSize == 0)
                {
                    // �t�@�C������Ȃ̂Ń_�E�����[�h�͕s�v

                    const auto alreadyExists = std::filesystem::exists(localPath);

                    if (!alreadyExists)
                    {
                        // ���[�J���ɑ��݂��Ȃ��̂� touch �Ɠ��`

                        // �^�C���X�^���v�𑮐����ɍ��킹��
                        // SetFileTime �����s����̂ŁAGENERIC_WRITE ���K�v

                        if (!ctx->openLocalFile(GENERIC_WRITE, CREATE_ALWAYS))
                        {
                            traceW(L"fault: openFile");
                            goto exit;
                        }

                        if (!ctx->setLocalFileTime(ctx->mFileInfo.CreationTime))
                        {
                            traceW(L"fault: setLocalTimeTime");
                            goto exit;
                        }
                    }

                    // �t�@�C������Ȃ̂ŁAEOF ��ԋp

                    ntstatus = STATUS_END_OF_FILE;
                    goto exit;
                }

                // �_�E�����[�h���K�v�����f

                bool needDownload = false;

                if (!shouldDownload(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
                {
                    traceW(L"fault: shouldDownload");
                    goto exit;
                }

                traceW(L"needDownload: %s", needDownload ? L"true" : L"false");

                if (needDownload)
                {
                    // �L���b�V���E�t�@�C���̏���

                    const FileOutputMeta meta
                    {
                        localPath,
                        CREATE_ALWAYS,
                        false,              // SetRange()
                        0,                  // Offset
                        0,                  // Length
                        true                // SetFileTime()
                    };

                    const auto bytesWritten = this->prepareLocalCacheFile(CONT_CALLER ctx->mObjKey, meta);

                    if (bytesWritten < 0)
                    {
                        traceW(L"fault: prepareLocalCacheFile_Simple bytesWritten=%lld", bytesWritten);
                        goto exit;
                    }
                }

                // �����̃t�@�C�����J��

                APP_ASSERT(std::filesystem::exists(localPath));

                if (!ctx->openLocalFile(0, OPEN_EXISTING))
                {
                    traceW(L"fault: openFile");
                    goto exit;
                }

                APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

                // �������̃T�C�Y�Ɣ�r

                LARGE_INTEGER fileSize;
                if(!::GetFileSizeEx(ctx->mLocalFile, &fileSize))
                {
                    traceW(L"fault: GetFileSizeEx");
                    goto exit;
                }

                if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
                {
                    traceW(L"fault: no match filesize ");
                    goto exit;
                }
            }

            // �t�@�C�����̃��b�N�����
        }

        // �t�@�C�����ւ̎Q�Ƃ�����
    }

    APP_ASSERT(ctx->mLocalFile);
    APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

    // Offset, Length �ɂ��t�@�C����ǂ�

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mLocalFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mLocalFile, Offset, Length, *PBytesTransferred);

    ntstatus = STATUS_SUCCESS;

exit:
    traceW(L"ntstatus=%ld", ntstatus);

    return ntstatus;
}

// EOF