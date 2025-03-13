#include "AwsS3.hpp"
#include <filesystem>


// ��̃p�[�g�E�T�C�Y
#define PART_LENGTH_BYTE		(1024ULL * 1024 * 4)


using namespace WinCseLib;


struct FilePart
{
    WINCSE_DEVICE_STATS* mStats;
    const UINT64 mOffset;
    const ULONG mLength;

    HANDLE mDone = NULL;
    bool mResult = false;

    std::atomic<bool> mInterrupt = false;

    FilePart(WINCSE_DEVICE_STATS* argStats, UINT64 argOffset, ULONG argLength)
        : mStats(argStats), mOffset(argOffset), mLength(argLength)
    {
        StatsIncr(_CreateEvent);

        mDone = ::CreateEventW(NULL,
            TRUE,				// �蓮���Z�b�g�C�x���g
            FALSE,				// ������ԁF��V�O�i�����
            NULL);

        APP_ASSERT(mDone);
    }

    void SetResult(bool argResult)
    {
        mResult = argResult;
        const auto b = ::SetEvent(mDone);					// �V�O�i����Ԃɐݒ�
        APP_ASSERT(b);
    }

    ~FilePart()
    {
        StatsIncr(_CloseHandle_Event);
        ::CloseHandle(mDone);
    }
};

struct ReadPartTask : public ITask
{
    AwsS3* mS3;
    const ObjectKey mObjKey;
    const std::wstring mLocalPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(AwsS3* that, const ObjectKey& argObjKey,
        const std::wstring argLocalPath, std::shared_ptr<FilePart> argFilePart)
        : mS3(that), mObjKey(argObjKey), mLocalPath(argLocalPath), mFilePart(argFilePart)
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
                const FileOutputMeta meta
                {
                    mLocalPath,
                    OPEN_EXISTING,
                    true,                   // SetRange()
                    mFilePart->mOffset,
                    mFilePart->mLength,
                    false                   // SetFileTime()
                };

                const auto bytesWritten = mS3->prepareLocalCacheFile(CONT_CALLER mObjKey, meta);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: prepareLocalCacheFile_Multipart bytesWritten=%lld", bytesWritten);
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

bool AwsS3::doMultipartDownload(CALLER_ARG WinCseLib::IOpenContext* argOpenContext, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);

    std::list<std::shared_ptr<FilePart>> fileParts;

    // �����擾����̈���쐬

    const int numParts = (int)((ctx->mFileInfo.FileSize + PART_LENGTH_BYTE - 1) / PART_LENGTH_BYTE);

    auto remaining = ctx->mFileInfo.FileSize;

    for (int i=0; i<numParts; i++)
    {
        fileParts.emplace_back
        (
            std::make_shared<FilePart>
            (
            mStats,
            PART_LENGTH_BYTE * i,                       // Offset
            (ULONG)min(PART_LENGTH_BYTE, remaining)     // Length
        )
        );

        remaining -= PART_LENGTH_BYTE;
    }

    for (auto& filePart: fileParts)
    {
        // �}���`�p�[�g�̓ǂݍ��݂�x���^�X�N�ɓo�^

        ITask* task = new ReadPartTask(this, ctx->mObjKey, localPath, filePart);
        APP_ASSERT(task);

        mDelayedWorker->addTask(CONT_CALLER task, Priority::Middle, CanIgnoreDuplicates::No);
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

    if (!ctx->setLocalFileTime(ctx->mFileInfo.CreationTime))
    {
        traceW(L"fault: setLocalTimeTime");
        return false;
    }

    return true;
}

//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
struct Shared : public SharedBase { };
static ShareStore<Shared> gSharedStore;

NTSTATUS AwsS3::readObject_Multipart(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
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

        UnprotectedShare<Shared> unsafeShare(&gSharedStore, remotePath);

        {
            // �t�@�C�����̃��b�N
            //
            // �����X���b�h���瓯��t�@�C���ւ̓����A�N�Z�X�͍s���Ȃ�
            // --> �t�@�C�������S�ɑ���ł��邱�Ƃ�ۏ�

            ProtectedShare<Shared> safeShare(&unsafeShare);

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

                if (!ctx->openLocalFile(GENERIC_WRITE, needDownload ? CREATE_ALWAYS : OPEN_EXISTING))
                {
                    traceW(L"fault: openFile");
                    goto exit;
                }

                APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

                traceW(L"needDownload: %s", needDownload ? L"true" : L"false");

                if (needDownload)
                {
                    // �_�E�����[�h���K�v

                    if (!doMultipartDownload(CONT_CALLER ctx, localPath))
                    {
                        traceW(L"fault: doMultipartDownload");
                        goto exit;
                    }
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