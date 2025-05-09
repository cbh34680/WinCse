#include "ExecuteApi.hpp"
#include "aws_sdk_s3.h"

using namespace CSELIB;
using namespace CSEDAS3;


static std::shared_ptr<Aws::StringStream> makeStreamFromFile(CALLER_ARG const std::filesystem::path& argInputPath,
    FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength)
{
    NEW_LOG_BLOCK();

    FileHandle file = ::CreateFileW(
        argInputPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: CreateFileW lerr=%lu", lerr);
        return nullptr;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argOffset;

    if (::SetFilePointerEx(file.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: SetFilePointerEx lerr=%lu file=%s", lerr, file.str().c_str());

        return nullptr;
    }

    auto stream = Aws::MakeShared<Aws::StringStream>("UploadSimpleStream");
    auto* pbuf = stream->rdbuf();

    auto remainingTotal = argLength;

    //std::vector<char> vBuffer(min(argLength, FILESIZE_1KiBu * 64));    // 64Kib
    //auto* buffer = vBuffer.data();
    //const auto bufferSize = static_cast<FILEIO_LENGTH_T>(vBuffer.size());

    static thread_local char buffer[FILEIO_BUFFER_SIZE];
    const FILEIO_LENGTH_T bufferSize = _countof(buffer);

    while (remainingTotal > 0)
    {
        if (!stream->good())
        {
            errorW(L"fault: no good");
            return nullptr;
        }

        DWORD bytesRead;
        if (!::ReadFile(file.handle(), buffer, static_cast<DWORD>(min(bufferSize, remainingTotal)), &bytesRead, NULL))
        {
            const auto lerr = ::GetLastError();

            errorW(L"fault: ReadFile lerr=%lu", lerr);
            return nullptr;
        }

        traceW(L"bytesRead=%lu", bytesRead);

        auto remainingWrite = static_cast<std::streamsize>(bytesRead);
        auto* pos = buffer;

        while (remainingWrite > 0)
        {
            traceW(L"remainingWrite=%lld", remainingWrite);

            const auto bytesWritten = pbuf->sputn(pos, remainingWrite);

            if (bytesWritten <= 0)
            {
                errorW(L"fault: sputn");
                return nullptr;
            }

            pos += bytesWritten;
            remainingWrite -= bytesWritten;

            traceW(L"bytesWritten=%lld remainingWrite=%lld", bytesWritten, remainingWrite);
        }

        remainingTotal -= bytesRead;

        traceW(L"remainingTotal=%lld", remainingTotal);
    }

    APP_ASSERT(remainingTotal == 0);

    return stream;
}

static Aws::Map<Aws::String, Aws::String> makeUploadMetadata(CALLER_ARG const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();

    const auto creationTime{ std::to_string(argFileInfo.CreationTime) };
    const auto lastAccessTime{ std::to_string(argFileInfo.LastAccessTime) };
    const auto lastWriteTime{ std::to_string(argFileInfo.LastWriteTime) };
    const auto changeTime{ std::to_string(argFileInfo.ChangeTime) };

    Aws::Map<Aws::String, Aws::String> metadata;

    metadata["wincse-creation-time"] = creationTime.c_str();
    metadata["wincse-last-access-time"] = lastAccessTime.c_str();
    metadata["wincse-last-write-time"] = lastWriteTime.c_str();
    metadata["wincse-change-time"] = changeTime.c_str();

    traceA("creationTime=%s lastAccessTime=%s lastWriteTime=%s changeTime=%s",
        creationTime.c_str(), lastAccessTime.c_str(), lastWriteTime.c_str(), changeTime.c_str());

#ifdef _DEBUG

    if (argSourcePath)
    {
        metadata["wincse-debug-source-path"] = WC2MB(argSourcePath).c_str();
    }

    metadata["wincse-debug-creation-time"]      = WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime).c_str();
    metadata["wincse-debug-last-access-time"]   = WinFileTime100nsToLocalTimeStringA(argFileInfo.LastAccessTime).c_str();
    metadata["wincse-debug-last-write-time"]    = WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime).c_str();
    metadata["wincse-debug-change-time"]        = WinFileTime100nsToLocalTimeStringA(argFileInfo.ChangeTime).c_str();

#endif

    return metadata;
}

bool ExecuteApi::uploadSimple(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s argFileInfo=%s argInputPath=%s", argObjKey.c_str(), FileInfoToStringW(argFileInfo).c_str(), argInputPath);

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (FA_IS_DIR(argFileInfo.FileAttributes))
    {
        // �f�B���N�g���̏ꍇ�͋�̃R���e���c

        if (argInputPath)
        {
            errorW(L"argSourcePath is set argInputPath=%s", argInputPath);
            return false;
        }
    }
    else
    {
        // �t�@�C���̏ꍇ�̓��[�J���E�L���b�V���̓��e���A�b�v���[�h����

        const auto body{ makeStreamFromFile(CONT_CALLER argInputPath, 0, argFileInfo.FileSize) };

        if (!body)
        {
            errorW(L"fault: makeStreamFromFile argSourcePath=%s", argInputPath);
            return false;
        }

        request.SetBody(body);
    }

    const auto metadata{ makeUploadMetadata(CONT_CALLER argFileInfo, argInputPath) };
    request.SetMetadata(metadata);

    traceW(L"PutObject argObjKey=%s, argInputPath=%s", argObjKey.c_str(), argInputPath);

    const auto outcome = mS3Client->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: PutObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    const auto& result{ outcome.GetResult() };

    traceW(L"success ETag=%s", result.GetETag().c_str());

    return true;
}

struct UploadFilePartTask : public IOnDemandTask
{
    ExecuteApi* mThat;
    const ObjectKey mObjKey;
    const std::filesystem::path mInputPath;
    Aws::String mUploadId;
    std::shared_ptr<UploadFilePartType> mFilePart;

    UploadFilePartTask(
        ExecuteApi* argThat,
        const ObjectKey& argObjKey,
        const std::filesystem::path& argInputPath,
        const Aws::String& argUploadId,
        const std::shared_ptr<UploadFilePartType>& argFilePart)
        :
        mThat(argThat),
        mObjKey(argObjKey),
        mInputPath(argInputPath),
        mUploadId(argUploadId),
        mFilePart(argFilePart)
    {
    }

    void run(int argThreadIndex) override
    {
        NEW_LOG_BLOCK();

        std::shared_ptr<Aws::S3::Model::CompletedPart> result;

        try
        {
            if (mFilePart->mInterrupt)
            {
                errorW(L"@%d Interruption request received", argThreadIndex);
            }
            else
            {
                result = mThat->uploadPart(START_CALLER mObjKey, mInputPath, mUploadId, mFilePart);
            }
        }
        catch (const std::exception& ex)
        {
            errorA("catch exception: what=[%s]", ex.what());
        }
        catch (...)
        {
            errorW(L"catch unknown");
        }

        // ���ʂ�ݒ肵�A�V�O�i����ԂɕύX
        // --> WaitForSingleObject �őҋ@���Ă���X���b�h�̃��b�N�����������

        mFilePart->setResult(std::move(result));
    }
};

std::shared_ptr<Aws::S3::Model::CompletedPart> ExecuteApi::uploadPart(CALLER_ARG
    const ObjectKey& argObjKey, const std::filesystem::path& argInputPath, const Aws::String& argUploadId,
    const std::shared_ptr<UploadFilePartType>& argFilePart)
{
    NEW_LOG_BLOCK();

    Aws::S3::Model::UploadPartRequest uploadRequest;

    uploadRequest.WithBucket(argObjKey.bucketA()).WithKey(argObjKey.keyA())
        .WithUploadId(argUploadId).WithPartNumber(argFilePart->mPartNumber);

    uploadRequest.SetContentLength(argFilePart->mLength);

    const auto body{ makeStreamFromFile(CONT_CALLER argInputPath, argFilePart->mOffset, argFilePart->mLength) };

    if (!body)
    {
        errorW(L"fault: makeStreamFromFile argInputPath=%s", argInputPath.c_str());
        return nullptr;
    }

    uploadRequest.SetBody(body);

    const auto uploadOutcome = mS3Client->UploadPart(uploadRequest);

    if (!outcomeIsSuccess(uploadOutcome))
    {
        errorW(L"fault: argObjKey=%s partNumber=%d", argObjKey.c_str(), argFilePart->mPartNumber);

        // Abort on failure
        Aws::S3::Model::AbortMultipartUploadRequest abortRequest;
        abortRequest.WithBucket(argObjKey.bucketA()).WithKey(argObjKey.keyA()).WithUploadId(argUploadId);
        mS3Client->AbortMultipartUpload(abortRequest);

        return nullptr;
    }

    auto result{ std::make_shared<Aws::S3::Model::CompletedPart>() };

    result->WithPartNumber(argFilePart->mPartNumber).WithETag(uploadOutcome.GetResult().GetETag());

    return result;
}

bool ExecuteApi::uploadMultipart(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argSourcePath);

    traceW(L"argObjKey=%s argFileInfo=%s argSourcePath=%s", argObjKey.c_str(), FileInfoToStringW(argFileInfo).c_str(), argSourcePath);

    const auto bucketName{ argObjKey.bucketA() };
    const auto objectName{ argObjKey.keyA() };
    const std::filesystem::path filePath{ argSourcePath };

    // �t�@�C���T�C�Y���擾

    const auto fileSize = GetFileSize(filePath);
    if (fileSize < 0)
    {
        errorW(L"fault: getFileSize");
        return false;
    }

    traceW(L"fileSize=%lld", fileSize);

    const auto PART_SIZE_BYTE = FILESIZE_1MiBll * mRuntimeEnv->TransferWriteSizeMib;

    // �����A�b�v���[�h����̈���쐬

    const auto partCount = UNIT_COUNT(fileSize, PART_SIZE_BYTE);
    APP_ASSERT(partCount > 1);

    std::list<std::shared_ptr<UploadFilePartType>> fileParts;

    for (int i=0; i<partCount; ++i)
    {
        // �����T�C�Y���Ƃ� FilePart ���쐬

        const auto partNumber = i + 1;
        const auto partOffset = i * PART_SIZE_BYTE;
        const auto partLength = min(PART_SIZE_BYTE, fileSize - partOffset);

        fileParts.emplace_back(std::make_shared<UploadFilePartType>(partNumber, partOffset, partLength, nullptr));
    }

    // �}���`�p�[�g�E�A�b�v���[�h�̏���

    Aws::S3::Model::CreateMultipartUploadRequest createRequest;
    createRequest.WithBucket(bucketName).WithKey(objectName);

    // ���^�f�[�^��ݒ�

    const auto metadata{ makeUploadMetadata(CONT_CALLER argFileInfo, argSourcePath) };
    createRequest.SetMetadata(metadata);

    const auto createOutcome = mS3Client->CreateMultipartUpload(createRequest);
    if (!outcomeIsSuccess(createOutcome))
    {
        errorW(L"fault: CreateMultipartUpload argObjKey=%s", argObjKey.c_str());
        return false;
    }

    // �p�[�g���ƂɃ^�X�N�𐶐�

    const auto& uploadId{ createOutcome.GetResult().GetUploadId() };

    for (const auto& filePart: fileParts)
    {
        traceW(L"addTask filePart=%s", filePart->str().c_str());

        mDelayedWorker->addTask(new UploadFilePartTask{ this, argObjKey, filePath, uploadId, filePart });
    }

    // �^�X�N�̊�����҂�

    std::vector<Aws::S3::Model::CompletedPart> completedParts(partCount);

    bool errorExists = false;
    int i = 0;

    for (const auto& filePart: fileParts)
    {
        auto result{ filePart->getResult() };

        if (!result)
        {
            errorExists = true;

            errorW(L"fault: mPartNumber=%d", filePart->mPartNumber);
            break;
        }

        completedParts[i] = std::move(*result);

        i++;
    }

    if (errorExists)
    {
        // �}���`�p�[�g�̈ꕔ�ɃG���[�����݂����̂ŁA�S�Ă̒x���^�X�N�𒆒f���ďI��

        for (auto& filePart: fileParts)
        {
            // �S�Ẵp�[�g�ɒ��f�t���O�𗧂Ă�

            traceW(L"set mInterrupt mPartNumber=%lld", filePart->mPartNumber);

            filePart->mInterrupt = true;
        }

        for (auto& filePart: fileParts)
        {
            // �^�X�N�̊�����ҋ@

            const auto result = filePart->getResult();
            if (!result)
            {
                errorW(L"fault: mPartNumber=%d", filePart->mPartNumber);
            }
        }

        traceW(L"error exists");
        return false;
    }

    // �A�b�v���[�h����

    Aws::S3::Model::CompletedMultipartUpload completedUpload;
    completedUpload.WithParts(completedParts);

    Aws::S3::Model::CompleteMultipartUploadRequest completeRequest;
    completeRequest.WithBucket(bucketName)
        .WithKey(objectName)
        .WithUploadId(uploadId)
        .WithMultipartUpload(completedUpload);

    const auto completeOutcome = mS3Client->CompleteMultipartUpload(completeRequest);
    if (!outcomeIsSuccess(completeOutcome))
    {
        errorW(L"fault: CompleteMultipartUpload argObjKey=%s", argObjKey.c_str());
        return false;
    }

    const auto& completeResult{ completeOutcome.GetResult() };

    traceW(L"Upload completed successfully. ETag=%s", completeResult.GetETag().c_str());

    return true;
}

// EOF