#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
struct Shared : public SharedBase { };
static ShareStore<Shared> gSharedStore;

bool AwsS3::readObject_Simple(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx->isFile());

    NEW_LOG_BLOCK();

    bool ret = false;
    OVERLAPPED Overlapped{};

    const auto remotePath{ ctx->getRemotePath() };
    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
        ctx, ctx->mLocalFile.handle(), Offset, Length, remotePath.c_str());

    UnprotectedShare<Shared> unsafeShare(&gSharedStore, remotePath);                // ���O�ւ̎Q�Ƃ�o�^
    {
        const auto safeShare{ unsafeShare.lock() };                                 // ���O�̃��b�N

        //
        // �֐��擪�ł� mLocalFile �̃`�F�b�N�����Ă��邪�A���b�N�L���ŏ󋵂�
        // �ς���Ă��邽�߁A���߂ă`�F�b�N����
        //
        if (ctx->mLocalFile.invalid())
        {
            traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                ctx->mLocalFile.handle(), Offset, Length, remotePath.c_str());

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

                    if (!ctx->openLocalFile(FILE_WRITE_ATTRIBUTES, CREATE_ALWAYS))
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

                ret = true;
                goto exit;
            }

            // �_�E�����[�h���K�v�����f

            bool needDownload = false;

            if (!shouldDownload(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
            {
                traceW(L"fault: shouldDownload");
                goto exit;
            }

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

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

            APP_ASSERT(ctx->mLocalFile.valid());

            // �������̃T�C�Y�Ɣ�r

            LARGE_INTEGER fileSize;
            if(!::GetFileSizeEx(ctx->mLocalFile.handle(), &fileSize))
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
    }   // ���O�̃��b�N������ (safeShare �̐�������)

    APP_ASSERT(ctx->mLocalFile.valid());

    // Offset, Length �ɂ��t�@�C����ǂ�

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mLocalFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mLocalFile.handle(), Offset, Length, *PBytesTransferred);

    ret = true;

exit:
    traceW(L"ret=%s", BOOL_CSTRW(ret));

    return ret;
}

// EOF