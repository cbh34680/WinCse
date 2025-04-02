#include "AwsS3.hpp"

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

struct ReadPartTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::No; }
    Priority getPriority() const noexcept override { return Priority::Middle; }

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

                const auto bytesWritten = mAwsS3->getObjectAndWriteToFile(CONT_CALLER mObjKey, outputParams);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: getObjectAndWriteToFile_Multipart bytesWritten=%lld", bytesWritten);
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

bool AwsS3::doMultipartDownload(CALLER_ARG OpenContext* ctx, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    // ��̃p�[�g�E�T�C�Y

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

        getWorker(L"delayed")->addTask(CONT_CALLER new ReadPartTask(this, ctx->mObjKey, localPath, filePart));
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

NTSTATUS AwsS3::prepareLocalFile_Multipart(CALLER_ARG OpenContext* ctx)
{
    NEW_LOG_BLOCK();

    const auto remotePath{ ctx->mObjKey.str() };

    traceW(L"remotePath=%s", remotePath.c_str());

    // �t�@�C�����ւ̎Q�Ƃ�o�^

    UnprotectedShare<PrepareLocalCacheFileShared> unsafeShare(&mGuardPrepareLocalCache, remotePath);    // ���O�ւ̎Q�Ƃ�o�^
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

            ntstatus = ctx->openFileHandle(CONT_CALLER
                //needDownload ? FILE_WRITE_ATTRIBUTES : 0,
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