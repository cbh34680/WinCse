#include "SdkS3Client.hpp"
#include "aws_sdk_s3.h"

using namespace CSELIB;
using namespace CSEDVC;

static std::shared_ptr<Aws::StringStream> makeStreamFromFile(CALLER_ARG const std::filesystem::path& argInputPath,
    FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength)
{
    NEW_LOG_BLOCK();

    auto stream = Aws::MakeShared<Aws::StringStream>("UploadSimpleStream");

    const auto nWrite = writeStreamFromFile(CONT_CALLER stream.get(), argInputPath, argOffset, argLength);
    if (nWrite != argLength)
    {
        errorW(L"fault: writeStreamFromFile argInputPath=%s", argInputPath.c_str());
        return nullptr;
    }

    traceW(L"stream length=%llu", stream->str().size());

    APP_ASSERT(static_cast<FILEIO_LENGTH_T>(stream->str().size()) == argLength);

    return stream;
}

namespace CSESS3 {

bool SdkS3Client::uploadSimple(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s argFileInfo=%s argInputPath=%s", argObjKey.c_str(), FileInfoToStringW(argFileInfo).c_str(), argInputPath);

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (FA_IS_DIR(argFileInfo.FileAttributes))
    {
        // �f�B���N�g���̏ꍇ�͋�̃R���e���c

        APP_ASSERT(!argInputPath);
    }
    else
    {
        APP_ASSERT(argInputPath);

        // �t�@�C���̏ꍇ�̓��[�J���E�L���b�V���̓��e���A�b�v���[�h����

        const auto body{ makeStreamFromFile(CONT_CALLER argInputPath, 0, argFileInfo.FileSize) };

        if (!body)
        {
            errorW(L"fault: makeStreamFromFile argInputPath=%s", argInputPath);
            return false;
        }

        APP_ASSERT(body->good());

        request.SetBody(body);
    }

    Aws::Map<Aws::String, Aws::String> metadata;
    setMetadataFromFileInfo(CONT_CALLER argFileInfo, &metadata);

    request.SetMetadata(metadata);

    traceW(L"PutObject argObjKey=%s, argInputPath=%s", argObjKey.c_str(), argInputPath);

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::PutObject, request, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(outcome))
    {
        errorW(L"fault: PutObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    return true;
}

struct UploadFilePartTask : public IOnDemandTask
{
    SdkS3Client* mThat;
    const ObjectKey mObjKey;
    const std::filesystem::path mInputPath;
    Aws::String mUploadId;
    std::shared_ptr<UploadFilePartType> mFilePart;

    UploadFilePartTask(
        SdkS3Client* argThat,
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

        std::optional<Aws::String> result;

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

std::optional<Aws::String> SdkS3Client::uploadPart(CALLER_ARG
    const ObjectKey& argObjKey, const std::filesystem::path& argInputPath, const Aws::String& argUploadId,
    const std::shared_ptr<UploadFilePartType>& argFilePart)
{
    NEW_LOG_BLOCK();

    Aws::S3::Model::UploadPartRequest uploadRequest;

    uploadRequest.WithBucket(argObjKey.bucketA()).WithKey(argObjKey.keyA())
        .WithUploadId(argUploadId).WithPartNumber(argFilePart->mPartNumber);

    const auto body{ makeStreamFromFile(CONT_CALLER argInputPath, argFilePart->mOffset, argFilePart->mLength) };

    if (!body)
    {
        errorW(L"fault: makeStreamFromFile argInputPath=%s", argInputPath.c_str());
        return nullptr;
    }

    uploadRequest.SetBody(body);

    const auto uploadOutcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::UploadPart, uploadRequest, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(uploadOutcome))
    {
        errorW(L"fault: argObjKey=%s partNumber=%d", argObjKey.c_str(), argFilePart->mPartNumber);

        // Abort on failure
        Aws::S3::Model::AbortMultipartUploadRequest abortRequest;
        abortRequest.WithBucket(argObjKey.bucketA()).WithKey(argObjKey.keyA()).WithUploadId(argUploadId);
        mS3Client->AbortMultipartUpload(abortRequest);

        return std::nullopt;
    }

    return uploadOutcome.GetResult().GetETag();
}

bool SdkS3Client::PutObjectInternal(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s argFileInfo=%s argInputPath=%s",
        argObjKey.c_str(), FileInfoToStringW(argFileInfo).c_str(), argInputPath);

    // �t�@�C���T�C�Y���擾

    const auto fileSize = argInputPath ? GetFileSize(argInputPath) : 0;
    if (fileSize < 0)
    {
        errorW(L"fault: getFileSize");
        return false;
    }

    traceW(L"fileSize=%lld", fileSize);

    const auto PART_SIZE_BYTE = FILESIZE_1MiBll * mRuntimeEnv->TransferWriteSizeMib;

    // �����A�b�v���[�h����̈���쐬

    const auto partCount = UNIT_COUNT(fileSize, PART_SIZE_BYTE);
    if (partCount <= 1)
    {
        // �������� 0 (�f�B���N�g��), 1 �̂Ƃ��͕��G�Ȃ��Ƃ͂��Ȃ�

        return this->uploadSimple(CONT_CALLER argObjKey, argFileInfo, argInputPath);
    }

    std::list<std::shared_ptr<UploadFilePartType>> fileParts;

    for (int i=0; i<partCount; ++i)
    {
        // �����T�C�Y���Ƃ� FilePart ���쐬

        const auto partNumber = i + 1;
        const auto partOffset = i * PART_SIZE_BYTE;
        const auto partLength = min(PART_SIZE_BYTE, fileSize - partOffset);

        fileParts.push_back(std::make_shared<UploadFilePartType>(partNumber, partOffset, partLength, std::nullopt));
    }

    // �}���`�p�[�g�E�A�b�v���[�h�̏���

    const auto bucketName{ argObjKey.bucketA() };
    const auto objectName{ argObjKey.keyA() };

    Aws::S3::Model::CreateMultipartUploadRequest createRequest;
    createRequest.WithBucket(bucketName).WithKey(objectName);

    // ���^�f�[�^��ݒ�

    //const auto metadata{ makeUploadMetadata(CONT_CALLER argFileInfo) };
    Aws::Map<Aws::String, Aws::String> metadata;
    setMetadataFromFileInfo(CONT_CALLER argFileInfo, &metadata);

    createRequest.SetMetadata(metadata);

    const auto createOutcome = mS3Client->CreateMultipartUpload(createRequest);
    if (!IsSuccess(createOutcome))
    {
        errorW(L"fault: CreateMultipartUpload argObjKey=%s", argObjKey.c_str());
        return false;
    }

    // �p�[�g���ƂɃ^�X�N�𐶐�

    const auto& uploadId{ createOutcome.GetResult().GetUploadId() };

    for (const auto& filePart: fileParts)
    {
        traceW(L"addTask filePart=%s", filePart->str().c_str());

        mDelayedWorker->addTask(new UploadFilePartTask{ this, argObjKey, argInputPath, uploadId, filePart });
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

        completedParts[i].WithPartNumber(filePart->mPartNumber).WithETag(*result);

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
    if (!IsSuccess(completeOutcome))
    {
        errorW(L"fault: CompleteMultipartUpload argObjKey=%s", argObjKey.c_str());
        return false;
    }

    traceW(L"Upload completed successfully.");

    return true;
}

}   // namespace CSESS3

// EOF