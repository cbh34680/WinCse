#include "ExecuteApi.hpp"
#include "aws_sdk_s3.h"

using namespace CSELIB;
using namespace CSESS3;


ExecuteApi::ExecuteApi(IWorker* argDelayedWorker, const RuntimeEnv* argRuntimeEnv, Aws::S3::S3Client* argS3Client)
    :
    mDelayedWorker(argDelayedWorker),
    mRuntimeEnv(argRuntimeEnv),
    mS3Client(argS3Client)
{
}

bool ExecuteApi::isInBucketFilters(const std::wstring& argBucket) const
{
    const auto& filters{ mRuntimeEnv->BucketFilters };

    if (filters.empty())
    {
        return true;
    }

    const auto it = std::find_if(filters.cbegin(), filters.cend(), [&argBucket](const auto& item)
    {
        return std::regex_match(argBucket, item);
    });

    return it != filters.cend();
}

bool ExecuteApi::shouldIgnoreFileName(const std::filesystem::path& argWinPath) const
{
    APP_ASSERT(!argWinPath.empty());
    APP_ASSERT(argWinPath.wstring().at(0) == L'\\');

    // ���X�g�̍ő吔�Ɋ֘A����̂ŁAAPI ���s���ʂ𐶐�����Ƃ��ɂ��`�F�b�N���K�v

    if (mRuntimeEnv->IgnoreFileNamePatterns)
    {
        return std::regex_search(argWinPath.wstring(), *mRuntimeEnv->IgnoreFileNamePatterns);
    }

    // ���K�\�����ݒ肳��Ă��Ȃ�

    return false;
}

bool ExecuteApi::Ping(CALLER_ARG0) const
{
    NEW_LOG_BLOCK();

    // S3 �ڑ�����
    traceW(L"Connection test");

    const auto outcome = mS3Client->ListBuckets();
    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: ListBuckets");
        return false;
    }

    return true;
}

bool ExecuteApi::ListBuckets(CALLER_ARG DirEntryListType* pDirEntryList) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntryList);

    Aws::S3::Model::ListBucketsRequest request;

    const auto outcome = mS3Client->ListBuckets(request);
    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: ListBuckets");
        return false;
    }

    DirEntryListType dirEntryList;

    const auto& result = outcome.GetResult();

    for (const auto& bucket : result.GetBuckets())
    {
        const auto bucketName{ MB2WC(bucket.GetName()) };

        if (!this->isInBucketFilters(bucketName))
        {
            // �o�P�b�g���ɂ��t�B���^�����O

            //traceW(L"%s: is not in filters, skip", bucketName.c_str());
            continue;
        }

        // �o�P�b�g�̍쐬�������擾

        const auto creationDateMillis{ bucket.GetCreationDate().Millis() };
        traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMillisToLocalTimeStringW(creationDateMillis).c_str());

        const auto creationDate = UtcMillisToWinFileTime100ns(creationDateMillis);

        auto dirEntry{ DirectoryEntry::makeBucketEntry(bucketName, creationDate) };
        APP_ASSERT(dirEntry);

        dirEntryList.emplace_back(std::move(dirEntry));

        // �ő�o�P�b�g�\�����̊m�F

        if (mRuntimeEnv->MaxDisplayBuckets > 0)
        {
            if (dirEntryList.size() >= mRuntimeEnv->MaxDisplayBuckets)
            {
                break;
            }
        }
    }

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool ExecuteApi::GetBucketRegion(CALLER_ARG const std::wstring& argBucket, std::wstring* pRegion) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pRegion);

    traceW(L"argBucket=%s", argBucket.c_str());

    namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

    Aws::S3::Model::GetBucketLocationRequest request;
    request.SetBucket(WC2MB(argBucket));

    const auto outcome = mS3Client->GetBucketLocation(request);
    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: GetBucketLocation argBucket=%s", argBucket.c_str());
        return false;
    }

    // ���P�[�V�������擾�ł����Ƃ�

    const auto& result = outcome.GetResult();
    const auto& location = result.GetLocationConstraint();

    if (location == Aws::S3::Model::BucketLocationConstraint::NOT_SET)
    {
        traceW(L"location is NOT_SET");
        return false;
    }

    *pRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

    traceW(L"bucketRegion=%s", pRegion->c_str());

    return true;
}

bool ExecuteApi::HeadObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirEntry);

    traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = mS3Client->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
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

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.cend())
    {
        dirEntry->mFileInfo.CreationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-access-time") != metadata.cend())
    {
        dirEntry->mFileInfo.LastAccessTime = std::stoull(metadata.at("wincse-last-access-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.cend())
    {
        dirEntry->mFileInfo.LastWriteTime = std::stoull(metadata.at("wincse-last-write-time"));
    }

    if (metadata.find("wincse-change-time") != metadata.cend())
    {
        dirEntry->mFileInfo.ChangeTime = std::stoull(metadata.at("wincse-change-time"));
    }

    dirEntry->mUserProperties.insert({ L"wincse-last-modified", std::to_wstring(lastModified) });
    dirEntry->mUserProperties.insert({ L"wincse-etag", MB2WC(result.GetETag()) });

    traceW(L"dirEntry=%s", dirEntry->str().c_str());

    *pDirEntry = std::move(dirEntry);

    return true;
}

//
// ListObjectsV2 API �����s�����ʂ������̃|�C���^�̎w���ϐ��ɕۑ�����
// �����̏����ɍ��v����I�u�W�F�N�g��������Ȃ��Ƃ��� false ��ԋp
//
bool ExecuteApi::ListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList) const
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

    UTC_MILLIS_T commonPrefixTime = UINT64_MAX;
    std::set<std::wstring> dirNames;

    Aws::String continuationToken;                              // Used for pagination.

    do
    {
        if (!continuationToken.empty())
        {
            request.SetContinuationToken(continuationToken);
        }

        const auto outcome = mS3Client->ListObjectsV2(request);
        if (!outcomeIsSuccess(outcome))
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

            const auto key{ keyFull.substr(argKeyLen) };

            // CommonPrefixes(=�f�B���N�g��) �Ȃ̂ŁA"/" �I�[����Ă���

            APP_ASSERT(!key.empty());
            APP_ASSERT(key != L"/");
            APP_ASSERT(key.back() == L'/');

            const auto keyWinPath{ argObjKey.append(key).toWinPath() };

            if (this->shouldIgnoreFileName(keyWinPath))
            {
                // ��������t�@�C�����̓X�L�b�v

                traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());

                continue;
            }

            // �f�B���N�g���Ɠ����t�@�C�����͖������邽�߂ɕۑ�

            dirNames.insert(key.substr(0, key.length() - 1));

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

        // �t�@�C�����̎��W

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

            const auto key{ keyFull.substr(argKeyLen) };

            APP_ASSERT(!key.empty());
            APP_ASSERT(key.back() != L'/');

            if (dirNames.find(key) != dirNames.cend())
            {
                // �f�B���N�g���Ɠ������O�̃t�@�C���͖���

                traceW(L"exists same name of dir key=%s", key.c_str());

                continue;
            }

            const auto keyWinPath{ argObjKey.append(key).toWinPath() };

            if (this->shouldIgnoreFileName(keyWinPath))
            {
                // ��������t�@�C�����̓X�L�b�v

                traceW(L"ignore keyWinPath=%s", keyWinPath.wstring().c_str());

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

bool ExecuteApi::DeleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) const
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

    const auto outcome = mS3Client->DeleteObjects(request);

    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: DeleteObjects argBucket=%s argKeys=%s", argBucket.c_str(), JoinStrings(argKeys, L',', true).c_str());
        return false;
    }

    return true;
}

bool ExecuteApi::DeleteObject(CALLER_ARG const ObjectKey& argObjKey) const
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    const auto outcome = mS3Client->DeleteObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: DeleteObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    return true;
}

bool ExecuteApi::PutObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    APP_ASSERT(argObjKey.isObject());

    // �p�[�g�T�C�Y�𒴂�����}���`�p�[�g�E�A�b�v���[�h

    const auto PART_SIZE_BYTE = FILESIZE_1MiBll * mRuntimeEnv->TransferWriteSizeMib;

    if (static_cast<FILESIZE_T>(argFileInfo.FileSize) <= PART_SIZE_BYTE || !argSourcePath)
    {
        return this->uploadSimple(CONT_CALLER argObjKey, argFileInfo, argSourcePath);
    }
    else
    {
        return this->uploadMultipart(CONT_CALLER argObjKey, argFileInfo, argSourcePath);
    }
}

static FILEIO_LENGTH_T writeFileFromStream(CALLER_ARG
    const Aws::IOStream& argInputStream, FILEIO_LENGTH_T argInputLength,
    const std::filesystem::path& argOutputPath, FILEIO_OFFSET_T argOutputOffset)
{
    NEW_LOG_BLOCK();

    // �t�@�C�����J�� argOffset �̈ʒu�Ƀ|�C���^���ړ�

    FileHandle file = ::CreateFileW
    (
        argOutputPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: CreateFileW lerr=%lu argOutputPath=%s argOffset=%lld", lerr, argOutputPath.c_str(), argOutputOffset);

        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argOutputOffset;

    if (::SetFilePointerEx(file.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: SetFilePointerEx lerr=%lu file=%s", lerr, file.str().c_str());

        return -1LL;
    }

    // �擾�������e���t�@�C���ɏo��

    auto* pbuf = argInputStream.rdbuf();
    auto remainingTotal = argInputLength;

    //std::vector<char> vBuffer(min(argInputLength, FILESIZE_1KiBu * 64));    // 64Kib
    //auto* buffer = vBuffer.data();
    //const FILEIO_LENGTH_T bufferSize = vBuffer.size();

    static thread_local char buffer[FILEIO_BUFFER_SIZE];
    const FILEIO_LENGTH_T bufferSize = _countof(buffer);

    while (remainingTotal > 0)
    {
        // �o�b�t�@�Ƀf�[�^��ǂݍ���

        if (!argInputStream.good())
        {
            errorW(L"fault: no good");
            return -1LL;
        }

        const auto bytesRead = pbuf->sgetn(buffer, min(remainingTotal, bufferSize));
        if (bytesRead <= 0)
        {
            errorW(L"fault: sgetn");
            return -1LL;
        }

        traceW(L"bytesRead=%lld", bytesRead);

        // �t�@�C���Ƀf�[�^����������

        auto* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            traceW(L"remainingWrite=%lld", remainingWrite);

            DWORD bytesWritten;
            if (!::WriteFile(file.handle(), pos, static_cast<DWORD>(remainingWrite), &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                errorW(L"fault: WriteFile lerr=%lu", lerr);

                return -1LL;
            }

            pos += bytesWritten;
            remainingWrite -= bytesWritten;

            traceW(L"bytesWritten=%lu remainingWrite=%lld", bytesWritten, remainingWrite);
        }

        remainingTotal -= bytesRead;

        traceW(L"remainingTotal=%lld", remainingTotal);
    }

    return argInputLength;
}

FILEIO_LENGTH_T ExecuteApi::GetObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey,
    const std::filesystem::path& argOutputPath, FILEIO_LENGTH_T argOffset, FILEIO_LENGTH_T argLength) const
{
    NEW_LOG_BLOCK();

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

    const auto outcome = mS3Client->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        errorW(L"fault: GetObject argObjKey=%s", argObjKey.c_str());
        return -1LL;
    }

    const auto& result{ outcome.GetResult() };

    // result �̓��e���t�@�C���ɏo�͂���

    return writeFileFromStream(CONT_CALLER result.GetBody(), result.GetContentLength(), argOutputPath, argOffset);
}

// EOF