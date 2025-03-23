#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


struct FilePart
{
    WINCSE_DEVICE_STATS* mStats;
    const UINT64 mOffset;
    const ULONG mLength;

    EventHandle mDone;
    bool mResult = false;

    std::atomic<bool> mInterrupt = false;

    FilePart(WINCSE_DEVICE_STATS* argStats, UINT64 argOffset, ULONG argLength)
        : mStats(argStats), mOffset(argOffset), mLength(argLength)
    {
        mDone = ::CreateEventW(NULL,
            TRUE,				// �蓮���Z�b�g�C�x���g
            FALSE,				// ������ԁF��V�O�i�����
            NULL);

        APP_ASSERT(mDone.valid());
    }

    void SetResult(bool argResult)
    {
        mResult = argResult;
        const auto b = ::SetEvent(mDone.handle());					// �V�O�i����Ԃɐݒ�
        APP_ASSERT(b);
    }

    ~FilePart()
    {
        mDone.close();
    }
};

struct ReadPartTask : public ITask
{
    AwsS3* mAwsS3;
    const ObjectKey mObjKey;
    const std::wstring mLocalPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(AwsS3* argAwsS3, const ObjectKey& argObjKey,
        const std::wstring argLocalPath, std::shared_ptr<FilePart> argFilePart)
        : mAwsS3(argAwsS3), mObjKey(argObjKey), mLocalPath(argLocalPath), mFilePart(argFilePart)
    {
    }

    void run(CALLER_ARG0)
    {
        NEW_LOG_BLOCK();

        bool result = false;

        try
        {
            if (mFilePart->mInterrupt)
            {
                traceW(L"Interruption request received");
            }
            else
            {
                const FileOutputParams outputParams
                {
                    mLocalPath,
                    OPEN_EXISTING,
                    true,                   // SetRange()
                    mFilePart->mOffset,
                    mFilePart->mLength
                };

                const auto bytesWritten = mAwsS3->prepareLocalCacheFile(CONT_CALLER mObjKey, outputParams);

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

bool AwsS3::doMultipartDownload(CALLER_ARG WinCseLib::CSDeviceContext* ctx, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    // ��̃p�[�g�E�T�C�Y
    const auto PART_LENGTH_BYTE = FILESIZE_1BU * 1024U * 1024 * 4;

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

        const auto reason = ::WaitForSingleObject(filePart->mDone.handle(), INFINITE);
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
        // �}���`�p�[�g�̈ꕔ�ɃG���[�����݂����̂ŁA�S�Ă̒x���^�X�N�𒆒f���ďI��

        for (auto& filePart: fileParts)
        {
            // �S�Ẵp�[�g�ɒ��f�t���O�𗧂Ă�
            filePart->mInterrupt = true;
        }

        for (auto& filePart: fileParts)
        {
            // �^�X�N�̊�����ҋ@

            const auto reason = ::WaitForSingleObject(filePart->mDone.handle(), INFINITE);
            APP_ASSERT(reason == WAIT_OBJECT_0);

            if (!filePart->mResult)
            {
                traceW(L"error offset=%lld", filePart->mOffset);
            }
        }

        traceW(L"error exists");
        return false;
    }

    return true;
}

//
// WinFsp �� Read() �ɂ��Ăяo����AOffset ���� Lengh �̃t�@�C���E�f�[�^��ԋp����
// �����ł͍ŏ��ɌĂяo���ꂽ�Ƃ��� s3 ����t�@�C�����_�E�����[�h���ăL���b�V���Ƃ������
// ���̃t�@�C�����I�[�v�����A���̌�� HANDLE ���g���܂킷
//
NTSTATUS AwsS3::readObject_Multipart(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
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
            // openFile() ��̏���̌Ăяo��

            traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                ctx->mFile.handle(), Offset, Length, remotePath.c_str());

            const auto localPath{ ctx->getFilePathW() };

            // �_�E�����[�h���K�v�����f

            bool needDownload = false;

            if (!syncFileAttributes(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
            {
                traceW(L"fault: syncFileAttributes");
                return STATUS_IO_DEVICE_ERROR;
            }

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

            DWORD dwCreationDisposition = 0;

            if (needDownload)
            {
                // �_�E�����[�h����̂ŁA�V�K�쐬���؂�l�߂ăt�@�C�����J��

                dwCreationDisposition = CREATE_ALWAYS;
            }
            else
            {
                // �_�E�����[�h���s�v�ȏꍇ�̓��[�J���E�L���b�V�����Q�l�ł����ԂɂȂ��Ă���͂�

                dwCreationDisposition = OPEN_EXISTING;

                if (ctx->mFileInfo.FileSize == 0)
                {
                    return STATUS_END_OF_FILE;
                }
            }

            // �t�@�C�����J��

            DWORD dwDesiredAccess = ctx->mGrantedAccess;
            if (needDownload)
            {
                dwDesiredAccess |= FILE_WRITE_ATTRIBUTES;           // SetFileTime() �ɕK�v
            }

            ULONG CreateFlags = 0;
            //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // �f�B���N�g���͑��삵�Ȃ�

            if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

            ctx->mFile = ::CreateFileW(ctx->getFilePathW().c_str(),
                dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
                dwCreationDisposition, CreateFlags, 0);

            if (ctx->mFile.invalid())
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: CreateFileW lerr=%lu", lerr);

                return FspNtStatusFromWin32(lerr);
            }

            if (needDownload)
            {
                // �_�E�����[�h���K�v

                if (!this->doMultipartDownload(CONT_CALLER ctx, localPath))
                {
                    traceW(L"fault: doMultipartDownload");
                    return STATUS_IO_DEVICE_ERROR;
                }

                // �t�@�C�������𓯊�

                if (!ctx->mFile.setFileTime(ctx->mFileInfo.CreationTime, ctx->mFileInfo.LastWriteTime))
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