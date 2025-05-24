#include "SdkS3Client.hpp"
#include "aws_sdk_s3.h"

using namespace CSELIB;
using namespace CSEDVC;

namespace CSESS3 {

bool SdkS3Client::ListBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntryList);

    Aws::S3::Model::ListBucketsRequest request;

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::ListBuckets, request, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(outcome))
    {
        errorW(L"fault: ListBuckets");
        return false;
    }

    DirEntryListType dirEntryList;

    const auto& result = outcome.GetResult();

    for (const auto& bucket : result.GetBuckets())
    {
        const auto bucketName{ MB2WC(bucket.GetName()) };

        if (!mRuntimeEnv->matchesBucketFilter(bucketName))
        {
            // �o�P�b�g���ɂ��t�B���^�����O

            //traceW(L"%s: is not in filters, skip", bucketName.c_str());
            continue;
        }

        // �o�P�b�g�̍쐬�������擾

        const auto creationDateMillis{ bucket.GetCreationDate().Millis() };
        traceW(L"bucketName=%s***, CreationDate=%s", SafeSubStringW(bucketName, 0, 3).c_str(), UtcMillisToLocalTimeStringW(creationDateMillis).c_str());

        const auto creationDate = UtcMillisToWinFileTime100ns(creationDateMillis);

        auto dirEntry{ DirectoryEntry::makeBucketEntry(bucketName, creationDate) };
        APP_ASSERT(dirEntry);

        dirEntryList.emplace_back(std::move(dirEntry));

        // �ő�o�P�b�g�\�����̊m�F

        if (mRuntimeEnv->MaxDisplayBuckets > 0)
        {
            if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayBuckets)
            {
                traceW(L"The maximum display limit has been reached.");
                break;
            }
        }
    }

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool SdkS3Client::GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pBucketRegion)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pBucketRegion);

    traceW(L"argBucket=%s", argBucket.c_str());

    namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

    Aws::S3::Model::GetBucketLocationRequest request;
    request.SetBucket(WC2MB(argBucket));

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::GetBucketLocation, request, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(outcome))
    {
        errorW(L"fault: GetBucketLocation argBucket=%s", argBucket.c_str());
        return false;
    }

    // ���P�[�V�������擾�ł����Ƃ�

    const auto& result = outcome.GetResult();
    const auto& location = result.GetLocationConstraint();

    std::string bucketRegionA;

    if (location == Aws::S3::Model::BucketLocationConstraint::NOT_SET)
    {
        traceW(L"location is NOT_SET, set default");

        bucketRegionA = this->getDefaultBucketRegion();
    }
    else
    {
        bucketRegionA = mapper::GetNameForBucketLocationConstraint(location);

        if (bucketRegionA.empty())
        {
            errorW(L"bucketRegionA is empty");
            return false;
        }
    }

    traceA("bucketRegionA=%s", bucketRegionA.c_str());

    *pBucketRegion = MB2WC(bucketRegionA);

    return true;
}

bool SdkS3Client::HeadObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntry);

    traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::HeadObject, request, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(outcome))
    {
        // HeadObject �̎��s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�

        traceW(L"fault: HeadObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    std::wstring filename;
    if (!SplitObjectKey(argObjKey.key(), nullptr, &filename))
    {
        errorW(L"fault: SplitObjectKey argObjKey=%s", argObjKey.c_str());
        return false;
    }

    const auto& result = outcome.GetResult();

    const auto lastModifiedMillis = result.GetLastModified().Millis();
    traceW(L"argObjKey=%s, LastModified=%s", argObjKey.c_str(), UtcMillisToLocalTimeStringW(lastModifiedMillis).c_str());
    const auto lastModified = UtcMillisToWinFileTime100ns(lastModifiedMillis);

    auto dirEntry = argObjKey.meansDir()
        ? DirectoryEntry::makeDirectoryEntry(filename, lastModified)
        : DirectoryEntry::makeFileEntry(filename, result.GetContentLength(), lastModified);
    APP_ASSERT(dirEntry);

    // ���^�E�f�[�^�� FILETIME �ɔ��f

    const auto& metadata{ result.GetMetadata() };
    setFileInfoFromMetadata(metadata, lastModified, result.GetETag(), &dirEntry);

    traceW(L"dirEntry=%s", dirEntry->str().c_str());

    *pDirEntry = std::move(dirEntry);

    return true;
}

//
// ListObjectsV2 API �����s�����ʂ������̃|�C���^�̎w���ϐ��ɕۑ�����
// �����̏����ɍ��v����I�u�W�F�N�g��������Ȃ��Ƃ��� false ��ԋp
//
bool SdkS3Client::ListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntryList);
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    DirEntryListType dirEntryList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(argObjKey.bucketA());
    request.WithDelimiter("/");

    const auto argKeyLen = argObjKey.key().length();
    if (argObjKey.isObject())
    {
        request.SetPrefix(argObjKey.keyA());
    }

    FILETIME_100NS_T commonPrefixTime = UINT64_MAX;
    std::set<std::wstring> prefixes;

    Aws::String continuationToken;                              // Used for pagination.

    do
    {
        if (!continuationToken.empty())
        {
            request.SetContinuationToken(continuationToken);
        }

        const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::ListObjectsV2, request, mRuntimeEnv->MaxApiRetryCount);

        if (!IsSuccess(outcome))
        {
            errorW(L"fault: ListObjectsV2 argObjKey=%s", argObjKey.c_str());
            return false;
        }

        const auto& result = outcome.GetResult();

        // �f�B���N�g���E�G���g���̂��ߍŏ��Ɉ�ԌÂ��^�C���X�^���v�����W
        // * CommonPrefix �ɂ̓^�C���X�^���v���Ȃ�����

        for (const auto& it : result.GetContents())
        {
            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());
            if (lastModified < commonPrefixTime)
            {
                commonPrefixTime = lastModified;
            }
        }

        if (commonPrefixTime == UINT64_MAX)
        {
            // �^�C���X�^���v���̎�ł��Ȃ���΃f�t�H���g�l���̗p

            commonPrefixTime = mRuntimeEnv->DefaultCommonPrefixTime;
        }

        // �f�B���N�g�����̎��W (CommonPrefix)

        for (const auto& it : result.GetCommonPrefixes())
        {
            const auto keyFull{ MB2WC(it.GetPrefix()) };
            if (keyFull == argObjKey.key())
            {
                // �����̃f�B���N�g�����Ɠ���(= "." �Ɠ��`)�͖���
                // --> �����͒ʉ߂��Ȃ����A�O�̂���

                continue;
            }

            // Prefix ��������菜��
            // 
            // "dir/"           --> ""              ... ��L�ŏ�����Ă���
            // "dir/subdir/"    --> "subdir/"       ... �ȍ~�͂����炪�Ώ�

            const auto key{ SafeSubStringW(keyFull, argKeyLen) };

            // CommonPrefixes(=�f�B���N�g��) �Ȃ̂ŁA"/" �I�[����Ă���

            APP_ASSERT(!key.empty());
            APP_ASSERT(key != L"/");
            APP_ASSERT(key.back() == L'/');

            const auto keyWinPath{ argObjKey.append(key).toWinPath() };
            if (mRuntimeEnv->shouldIgnoreWinPath(keyWinPath))
            {
                // ��������t�@�C�����̓X�L�b�v

                traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());
                continue;
            }

            // �f�B���N�g���Ɠ����t�@�C�����͖������邽�߂ɕۑ�

            prefixes.insert(SafeSubStringW(key, 0, key.length() - 1));

            // CommonPrefix �Ȃ̂ŁA�f�B���N�g���E�I�u�W�F�N�g�Ƃ��ēo�^

            auto dirEntry{ DirectoryEntry::makeDirectoryEntry(key, commonPrefixTime) };
            APP_ASSERT(dirEntry);

            dirEntryList.push_back(std::move(dirEntry));

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

                    goto exit;
                }
            }
        }

        // �t�@�C�����̎��W ("dir/" �̂悤�ȋ�I�u�W�F�N�g���܂�)

        for (const auto& it : result.GetContents())
        {
            const auto keyFull{ MB2WC(it.GetKey()) };
            if (keyFull == argObjKey.key())
            {
                // �����̃f�B���N�g�����Ɠ���(= "." �Ɠ��`)�͖���

                continue;
            }

            // Prefix ��������菜��
            // 
            // "dir/"           --> ""              ... ��L�ŏ�����Ă���
            // "dir/file1.txt"  --> "file1.txt"     ... �ȍ~�͂����炪�Ώ�

            const auto key{ SafeSubStringW(keyFull, argKeyLen) };

            APP_ASSERT(!key.empty());
            APP_ASSERT(key.back() != L'/');

            const auto keyWinPath{ argObjKey.append(key).toWinPath() };
            if (mRuntimeEnv->shouldIgnoreWinPath(keyWinPath))
            {
                // ��������t�@�C�����̓X�L�b�v

                traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());
                continue;
            }

            if (prefixes.find(key) != prefixes.cend())
            {
                // �f�B���N�g���Ɠ������O�̃t�@�C���͖���

                traceW(L"exists same name of dir key=%s", key.c_str());

                continue;
            }

            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            auto dirEntry = DirectoryEntry::makeFileEntry(key, it.GetSize(), lastModified);
            APP_ASSERT(dirEntry);

            dirEntryList.emplace_back(std::move(dirEntry));

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayObjects)
                {
                    // ���ʃ��X�g�� ini �t�@�C���Ŏw�肵���ő�l�ɓ��B

                    traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

                    goto exit;
                }
            }
        }

        continuationToken = result.GetNextContinuationToken();
    } while (!continuationToken.empty());

exit:
    traceW(L"dirEntryList.size=%zu", dirEntryList.size());

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool SdkS3Client::DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKeys.empty());

    traceW(L"DeleteObjects argBucket=%s argKeys=%s", argBucket.c_str(), JoinStrings(argKeys, L',', true).c_str());

    Aws::S3::Model::Delete delete_objects;

    for (const auto& it: argKeys)
    {
        Aws::S3::Model::ObjectIdentifier obj;
        obj.SetKey(WC2MB(it));
        delete_objects.AddObjects(obj);
    }

    Aws::S3::Model::DeleteObjectsRequest request;
    request.SetBucket(WC2MB(argBucket));
    request.SetDelete(delete_objects);

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::DeleteObjects, request, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(outcome))
    {
        errorW(L"fault: DeleteObjects argBucket=%s argKeys=%s", argBucket.c_str(), JoinStrings(argKeys, L',', true).c_str());
        return false;
    }

    return true;
}

bool SdkS3Client::DeleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::DeleteObject, request, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(outcome))
    {
        errorW(L"fault: DeleteObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    return true;
}

bool SdkS3Client::PutObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath)
{
    APP_ASSERT(argObjKey.isObject());

    return this->PutObjectInternal(CONT_CALLER argObjKey, argFileInfo, argInputPath);
}

bool SdkS3Client::CopyObject(CALLER_ARG const ObjectKey& argSrcObjKey, const ObjectKey& argDstObjKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argSrcObjKey.isObject());
    APP_ASSERT(argDstObjKey.isObject());
    APP_ASSERT(argSrcObjKey.toFileType() == argDstObjKey.toFileType());

    Aws::S3::Model::CopyObjectRequest request;

    request.SetCopySource(argSrcObjKey.strA());
    request.SetBucket(argDstObjKey.bucketA());
    request.SetKey(argDstObjKey.keyA());

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::CopyObject, request, mRuntimeEnv->MaxApiRetryCount);

    if (!IsSuccess(outcome))
    {
        errorW(L"fault: CopyObject argSrcObjKey=%s argDstObjKey=%s", argSrcObjKey.c_str(), argDstObjKey.c_str());
        return false;
    }

    traceW(L"success: CopyObject argSrcObjKey=%s argDstObjKey=%s", argSrcObjKey.c_str(), argDstObjKey.c_str());

    return true;
}

FILEIO_LENGTH_T SdkS3Client::GetObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey,
    const std::filesystem::path& argOutputPath, FILEIO_LENGTH_T argOffset, FILEIO_LENGTH_T argLength)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argOffset >= 0LL);
    APP_ASSERT(argLength > 0);

    const auto endOffset = argOffset + argLength - 1;

    std::ostringstream ss;
    ss << "bytes=";
    ss << argOffset;
    ss << "-";
    ss << endOffset;

    const auto range{ ss.str() };
    traceA("range=%s", range.c_str());

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    request.SetRange(range);

    const auto outcome = executeWithRetry(mS3Client, &Aws::S3::S3Client::GetObject, request, mRuntimeEnv->MaxApiRetryCount);
    if (!IsSuccess(outcome))
    {
        errorW(L"fault: GetObject argObjKey=%s", argObjKey.c_str());
        return -1LL;
    }

    const auto& result{ outcome.GetResult() };
    const auto contentLength = result.GetContentLength();

    if (argLength != result.GetContentLength())
    {
        errorW(L"fault: unmatch argLength=%lld result=%lld", argLength, contentLength);
        return -1LL;
    }

    // result �̓��e���t�@�C���ɏo�͂���

    auto& body{ result.GetBody() };

    return writeFileFromStream(CONT_CALLER argOutputPath, argOffset, &body, contentLength);
}

}   // namespace CSESS3

// EOF