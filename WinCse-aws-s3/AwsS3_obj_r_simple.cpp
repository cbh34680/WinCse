#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
bool AwsS3::readFile_Simple(CALLER_ARG PVOID UParam,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    APP_ASSERT(UParam);

    ReadFileContext* ctx = (ReadFileContext*)UParam;
    APP_ASSERT(!ctx->mBucket.empty());
    APP_ASSERT(!ctx->mKey.empty());
    APP_ASSERT(ctx->mKey.back() != L'/');

    NEW_LOG_BLOCK();

    const auto remotePath{ ctx->getGuardString() };
    traceW(L"HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx->mFile, Offset, Length, remotePath.c_str());

    //
    // �����ɕ����̃X���b�h����قȂ�I�t�Z�b�g�ŌĂяo�����̂�
    // ���� mFile �ɐݒ肳��Ă��邩���肵�A���ʂȃ��b�N�͔�����
    //
    if (ctx->mFile == INVALID_HANDLE_VALUE)
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
            // �֐��擪�ł� mFile �̃`�F�b�N�����Ă��邪�A���b�N�L���ŏ󋵂�
            // �ς���Ă��邽�߁A���߂ă`�F�b�N����
            //
            if (ctx->mFile == INVALID_HANDLE_VALUE)
            {
                traceW(L"init mFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                    ctx->mFile, Offset, Length, remotePath.c_str());

                // openFile() ��̏���̌Ăяo��

                const std::wstring localPath{ mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(remotePath) };

                // �_�E�����[�h���K�v�����f

                bool needGet = false;

                if (!shouldDownload(CONT_CALLER ctx->mBucket, ctx->mKey, localPath, &ctx->mFileInfo, &needGet))
                {
                    traceW(L"fault: shouldDownload");
                    return false;
                }

                traceW(L"needGet: %s", needGet ? L"true" : L"false");

                if (needGet)
                {
                    // �L���b�V���E�t�@�C���̏���

                    const FileOutputMeta meta{ localPath, CREATE_ALWAYS, false, 0, 0, true };

                    const auto bytesWritten = this->prepareLocalCacheFile(CONT_CALLER
                        ctx->mBucket, ctx->mKey, meta);

                    if (bytesWritten < 0)
                    {
                        traceW(L"fault: prepareLocalCacheFile_Simple return=%lld", bytesWritten);
                        return false;
                    }
                }

                APP_ASSERT(std::filesystem::exists(localPath));

                // �L���b�V���E�t�@�C�����J���AHANDLE ���R���e�L�X�g�ɕۑ�

                ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
                if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                    CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

#if 0
                traceW(L"CreateFile path=%s dwDesiredAccess=%ld dwFlagsAndAttributes=%ld",
                    localPath.c_str(), ctx->mGrantedAccess, CreateFlags);

                traceW(L"compare dwDesiredAccess=%ld dwFlagsAndAttributes=%ld",
                    FILE_READ_DATA | FILE_WRITE_DATA | FILE_WRITE_EA | FILE_READ_ATTRIBUTES | GENERIC_ALL,
                    FILE_FLAG_BACKUP_SEMANTICS);
#endif
                ctx->mFile = ::CreateFileW(localPath.c_str(),
                    ctx->mGrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, CreateFlags, NULL);

                if (ctx->mFile == INVALID_HANDLE_VALUE)
                {
                    traceW(L"fault: CreateFileW");
                    return false;
                }

                StatsIncr(_CreateFile);

                // �������̃T�C�Y�Ɣ�r

                LARGE_INTEGER fileSize;
                if(!::GetFileSizeEx(ctx->mFile, &fileSize))
                {
                    traceW(L"fault: GetFileSizeEx");
                    return false;
                }

                if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
                {
                    traceW(L"fault: no match filesize ");
                    return false;
                }
            }

            // �t�@�C�����̃��b�N�����
        }

        // �t�@�C�����ւ̎Q�Ƃ�����
    }

    APP_ASSERT(ctx->mFile);
    APP_ASSERT(ctx->mFile != INVALID_HANDLE_VALUE);

    // Offset, Length �ɂ��t�@�C����ǂ�

    OVERLAPPED Overlapped{};
    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        return false;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mFile, Offset, Length, *PBytesTransferred, Offset - ctx->mLastOffset);

    ctx->mLastOffset = Offset;

    return true;
}

// EOF