#include "CSDevice.hpp"

using namespace WCSE;


struct ReadPartTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::No; }
    Priority getPriority() const noexcept override { return Priority::Middle; }

    ExecuteApi* mExecuteApi;
    const ObjectKey mObjKey;
    const std::wstring mLocalPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(ExecuteApi* argExecuteApi, const ObjectKey& argObjKey,
        const std::wstring argLocalPath, std::shared_ptr<FilePart> argFilePart)
        :
        mExecuteApi(argExecuteApi),
        mObjKey(argObjKey),
        mLocalPath(argLocalPath),
        mFilePart(argFilePart)
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
                    mFilePart->mOffset,
                    mFilePart->mLength
                };

                const auto bytesWritten = mExecuteApi->GetObjectAndWriteToFile(CONT_CALLER mObjKey, outputParams);

                if (bytesWritten > 0)
                {
                    result = true;
                }
                else
                {
                    traceW(L"fault: GetObjectAndWriteToFile bytesWritten=%lld", bytesWritten);
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

    void cancelled(CALLER_ARG0) noexcept
    {
        NEW_LOG_BLOCK();

        traceW(L"set Interrupt");

        mFilePart->mInterrupt = true;
    }
};

//
// �擾�f�[�^���p�[�g�ɕ����ĕ����_�E�����[�h����
//
bool CSDevice::downloadMultipart(CALLER_ARG OpenContext* ctx, const std::wstring& localPath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(ctx);

    // ��̃p�[�g�E�T�C�Y

    std::list<std::shared_ptr<FilePart>> fileParts;

    // �����擾����̈���쐬

    const int numParts = (int)((ctx->mFileInfo.FileSize + PART_SIZE_BYTE - 1) / PART_SIZE_BYTE);

    auto remaining = ctx->mFileInfo.FileSize;

    for (int i=0; i<numParts; i++)
    {
        fileParts.emplace_back(
            std::make_shared<FilePart>(
            PART_SIZE_BYTE * i,                       // Offset
            (ULONG)min(PART_SIZE_BYTE, remaining)     // Length
        )
        );

        remaining -= PART_SIZE_BYTE;
    }

    for (auto& filePart: fileParts)
    {
        // �}���`�p�[�g�̓ǂݍ��݂�x���^�X�N�ɓo�^

        auto task{ new ReadPartTask(this->mExecuteApi.get(), ctx->mObjKey, localPath, filePart) };
        APP_ASSERT(task);

        getWorker(L"delayed")->addTask(CONT_CALLER task);
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



// EOF