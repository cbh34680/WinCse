#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


struct ReadPartTask : public ITask
{
    AwsS3* mS3;
    const std::wstring mBucket;
    const std::wstring mKey;
    const std::wstring mLocalPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(AwsS3* that, const std::wstring& argBucket, const std::wstring& argKey,
        const std::wstring argLocalPath, std::shared_ptr<FilePart> argFilePart)
        : mS3(that), mBucket(argBucket), mKey(argKey), mLocalPath(argLocalPath), mFilePart(argFilePart)
    {
    }

    void run(CALLER_ARG0)
    {
        NEW_LOG_BLOCK();

        traceW(L"ReadPartTask::run");

        bool result = false;

        try
        {
            if (mFilePart->mInterrupt)
            {
                traceW(L"Interruption request received");
            }
            else
            {
                const FileOutputMeta meta{ mLocalPath, OPEN_EXISTING, true, mFilePart->mOffset, mFilePart->mLength, false };

                const auto bytesWritten = mS3->prepareLocalCacheFile(CONT_CALLER
                    mBucket, mKey, meta);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: prepareLocalCacheFile_Multipart return %lld", bytesWritten);
                }
            }
        }
        catch (const std::exception& ex)
        {
            traceA("catch exception: what=[%s]", ex.what());
        }
        catch (...)
        {
            traceW(L"catch unknown");
        }

        // ���ʂ�ݒ肵�A�V�O�i����ԂɕύX
        // --> WaitForSingleObject �őҋ@���Ă���X���b�h�̃��b�N�����������

        mFilePart->SetResult(result);
    }
};


//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
bool AwsS3::readFile_Multipart(CALLER_ARG PVOID UParam,
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

        UnprotectedNamedData<Shared_Multipart> unsafeShare(remotePath);

        {
            // �t�@�C�����̃��b�N
            //
            // �����X���b�h���瓯��t�@�C���ւ̓����A�N�Z�X�͍s���Ȃ�
            // --> �t�@�C�������S�ɑ���ł��邱�Ƃ�ۏ�

            ProtectedNamedData<Shared_Multipart> safeShare(unsafeShare);

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

                // �L���b�V���E�t�@�C�����J���AHANDLE ���R���e�L�X�g�ɕۑ�

                ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
                if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                    CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

                const DWORD dwDesiredAccess = ctx->mGrantedAccess | GENERIC_WRITE;
                const DWORD dwCreationDisposition = needGet ? CREATE_ALWAYS : OPEN_EXISTING;

                ctx->mFile = ::CreateFileW(localPath.c_str(),
                    dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, dwCreationDisposition, CreateFlags, NULL);

                if (ctx->mFile == INVALID_HANDLE_VALUE)
                {
                    traceW(L"fault: CreateFileW");
                    return false;
                }

                StatsIncr(_CreateFile);

                if (needGet)
                {
                    // �_�E�����[�h���K�v

                    std::list<std::shared_ptr<FilePart>> fileParts;

                    // �����擾����̈���쐬

                    const int numParts = (int)((ctx->mFileInfo.FileSize + SIMPLE_DOWNLOAD_THRESHOLD - 1) / SIMPLE_DOWNLOAD_THRESHOLD);

                    auto remaining = ctx->mFileInfo.FileSize;

                    for (int i=0; i<numParts; i++)
                    {
                        fileParts.emplace_back
                        (
                            std::make_shared<FilePart>
                            (
                                mStats,
                                SIMPLE_DOWNLOAD_THRESHOLD * i,
                                (ULONG)min(SIMPLE_DOWNLOAD_THRESHOLD, remaining)
                            )
                        );

                        remaining -= SIMPLE_DOWNLOAD_THRESHOLD;
                    }

                    for (auto& filePart: fileParts)
                    {
                        // �}���`�p�[�g�̓ǂݍ��݂�x���^�X�N�ɓo�^

                        ITask* task = new ReadPartTask(this, ctx->mBucket, ctx->mKey, localPath, filePart);
                        APP_ASSERT(task);

                        mDelayedWorker->addTask(CONT_CALLER task, Priority::Low, CanIgnore::No);
                    }

                    bool errorExists = false;

                    for (auto& filePart: fileParts)
                    {
                        // �^�X�N�̊�����ҋ@

                        const auto reason = ::WaitForSingleObject(filePart->mDone, INFINITE);
                        APP_ASSERT(reason == WAIT_OBJECT_0);

                        if (!filePart->mResult)
                        {
                            // �G���[������p�[�g�𔭌�

                            errorExists = true;
                            break;
                        }
                    }

                    if (errorExists)
                    {
                        // �}���`�p�[�g�̈ꕔ�ɃG���[�����݂����̂ŁA�S�Ă̒x���^�X�N��
                        // �L�����Z�����ďI��

                        for (auto& filePart: fileParts)
                        {
                            // �S�Ẵp�[�g�ɒ��f�t���O�𗧂Ă�
                            filePart->mInterrupt = true;
                        }

                        for (auto& filePart: fileParts)
                        {
                            // �^�X�N�̊�����ҋ@

                            const auto reason = ::WaitForSingleObject(filePart->mDone, INFINITE);
                            APP_ASSERT(reason == WAIT_OBJECT_0);

                            if (!filePart->mResult)
                            {
                                traceW(L"error offset=%lld", filePart->mOffset);
                            }
                        }

                        traceW(L"error exists");
                        return false;
                    }

                    // �^�C���X�^���v���X�V

                    FILETIME ft;
                    WinFileTime100nsToWinFile(ctx->mFileInfo.CreationTime, &ft);

                    FILETIME ftNow;
                    ::GetSystemTimeAsFileTime(&ftNow);

                    if (!::SetFileTime(ctx->mFile, &ft, &ftNow, &ft))
                    {
                        const auto lerr = ::GetLastError();
                        traceW(L"fault: SetFileTime lerr=%ld", lerr);
                        return false;
                    }
                }

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