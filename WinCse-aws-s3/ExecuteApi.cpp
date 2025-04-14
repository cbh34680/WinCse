#include "ExecuteApi.hpp"
#include "aws_sdk_s3.h"
#include <fstream>

using namespace WCSE;


ExecuteApi::ExecuteApi(
    const RuntimeEnv* argRuntimeEnv,
    const std::wstring& argRegion,
    const std::wstring& argAccessKeyId,
    const std::wstring& argSecretAccessKey) noexcept
    :
    mRuntimeEnv(argRuntimeEnv)
{
    NEW_LOG_BLOCK();

    // S3 �N���C�A���g�̐���

    mSdkOptions = std::make_unique<Aws::SDKOptions>();
    Aws::InitAPI(*mSdkOptions);

    std::string region{ WC2MB(argRegion) };

    Aws::Client::ClientConfiguration config;
    if (argRegion.empty())
    {
        // �Ƃ肠�����f�t�H���g�E���[�W�����Ƃ��Đݒ肵�Ă���

        region = AWS_DEFAULT_REGION;
    }

    traceA("region=%s", region.c_str());

    // ����) Aws::Region::AP_NORTHEAST_1;
    // ���) Aws::Region::AP_NORTHEAST_3;

    config.region = region;

    Aws::S3::S3Client* client = nullptr;

    if (!argAccessKeyId.empty() && !argSecretAccessKey.empty())
    {
        const Aws::Auth::AWSCredentials credentials{ WC2MB(argAccessKeyId), WC2MB(argSecretAccessKey) };

        client = new Aws::S3::S3Client(credentials, nullptr, config);

        traceW(L"use credentials");
    }
    else
    {
        client = new Aws::S3::S3Client(config);
    }

    APP_ASSERT(client);
    mS3Client = std::unique_ptr<Aws::S3::S3Client>(client);
}

ExecuteApi::~ExecuteApi()
{
    NEW_LOG_BLOCK();

    // �f�X�g���N�^������Ă΂��̂ŁA�ē��\�Ƃ��Ă�������

    // AWS S3 �����I��

    if (mSdkOptions)
    {
        traceW(L"aws shutdown");

        Aws::ShutdownAPI(*mSdkOptions);
        mSdkOptions.reset();
    }
}

bool ExecuteApi::Ping(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    // S3 �ڑ�����
    traceW(L"Connection test");

    const auto outcome = mS3Client->ListBuckets();
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: ListBuckets");
        return false;
    }

    return true;
}

bool ExecuteApi::ListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);

    Aws::S3::Model::ListBucketsRequest request;

    const auto outcome = mS3Client->ListBuckets(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: ListBuckets");
        return false;
    }

    DirInfoListType dirInfoList;

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

        const auto creationMillis{ bucket.GetCreationDate().Millis() };
        traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMilliToLocalTimeStringW(creationMillis).c_str());

        const auto FileTime = UtcMillisToWinFileTime100ns(creationMillis);

        // �f�B���N�g���E�G���g���𐶐�

        auto dirInfo = makeDirInfoDir(bucketName, FileTime);
        APP_ASSERT(dirInfo);

        // �o�P�b�g�͏�ɓǂݎ���p
        // --> �f�B���N�g���ɑ΂��Ă͈Ӗ����Ȃ�

        //dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;

        dirInfoList.emplace_back(dirInfo);

        // �ő�o�P�b�g�\�����̊m�F

        if (mRuntimeEnv->MaxDisplayBuckets > 0)
        {
            if (dirInfoList.size() >= mRuntimeEnv->MaxDisplayBuckets)
            {
                break;
            }
        }
    }

    *pDirInfoList = std::move(dirInfoList);

    return true;
}

bool ExecuteApi::GetBucketRegion(CALLER_ARG
    const std::wstring& argBucketName, std::wstring* pBucketRegion)
{
    NEW_LOG_BLOCK();

    //traceW(L"do GetBucketLocation()");

    namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

    Aws::S3::Model::GetBucketLocationRequest request;
    request.SetBucket(WC2MB(argBucketName));

    const auto outcome = mS3Client->GetBucketLocation(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetBucketLocation");
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

    *pBucketRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

    //traceW(L"success, region is %s", bucketRegion.c_str());

    return true;
}

bool ExecuteApi::HeadObject(CALLER_ARG
    const ObjectKey& argObjKey, DirInfoType* pDirInfo)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfo);
    //APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    const auto outcome = mS3Client->HeadObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        // HeadObject �̎��s���G���[�A�܂��̓I�u�W�F�N�g��������Ȃ�

        traceW(L"fault: HeadObject");
        return false;
    }

    std::wstring filename;
    if (!SplitPath(argObjKey.key(), nullptr, &filename))
    {
        traceW(L"fault: SplitPath");
        return false;
    }

    auto dirInfo = makeEmptyDirInfo(filename);
    APP_ASSERT(dirInfo);

    const auto& result = outcome.GetResult();

    const auto fileSize = result.GetContentLength();
    const auto lastModified = UtcMillisToWinFileTime100ns(result.GetLastModified().Millis());

    UINT64 creationTime = lastModified;
    UINT64 lastAccessTime = lastModified;
    UINT64 lastWriteTime = lastModified;
    UINT32 fileAttributes = mRuntimeEnv->DefaultFileAttributes;

    if (argObjKey.meansDir())
    {
        fileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    }

    const auto& metadata = result.GetMetadata();

    if (metadata.find("wincse-creation-time") != metadata.cend())
    {
        creationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.cend())
    {
        lastWriteTime = std::stoull(metadata.at("wincse-last-write-time"));
    }

    if (argObjKey.meansHidden())
    {
        // �B���t�@�C��

        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    if (fileAttributes == 0)
    {
        fileAttributes = FILE_ATTRIBUTE_NORMAL;
    }

    dirInfo->FileInfo.FileAttributes = fileAttributes;
    dirInfo->FileInfo.FileSize = fileSize;
    dirInfo->FileInfo.AllocationSize = (fileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
    dirInfo->FileInfo.CreationTime = creationTime;
    dirInfo->FileInfo.LastAccessTime = lastAccessTime;
    dirInfo->FileInfo.LastWriteTime = lastWriteTime;
    dirInfo->FileInfo.ChangeTime = lastModified;
    dirInfo->FileInfo.IndexNumber = HashString(argObjKey.str());

    dirInfo->mUserProperties.insert({ L"wincse-last-modified", std::to_wstring(lastModified) });

    if (metadata.find("wincse-client-guid") != metadata.cend())
    {
        dirInfo->mUserProperties.insert(
            { L"wincse-client-guid", MB2WC(metadata.at("wincse-client-guid")) });
    }

    *pDirInfo = std::move(dirInfo);

    return true;
}

//
// ListObjectsV2 API �����s�����ʂ������̃|�C���^�̎w���ϐ��ɕۑ�����
// �����̏����ɍ��v����I�u�W�F�N�g��������Ȃ��Ƃ��� false ��ԋp
//
bool ExecuteApi::ListObjectsV2(CALLER_ARG const ObjectKey& argObjKey,
    bool argDelimiter, int argLimit, DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pDirInfoList);
    APP_ASSERT(argObjKey.valid());

    traceW(L"argObjKey=%s, argDelimiter=%s, argLimit=%d",
        argObjKey.c_str(), BOOL_CSTRW(argDelimiter), argLimit);

    DirInfoListType dirInfoList;

    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(argObjKey.bucketA());

    if (argDelimiter)
    {
        request.WithDelimiter("/");
    }

    if (argLimit > 0)
    {
        request.SetMaxKeys(argLimit);
    }

    const auto argKeyLen = argObjKey.key().length();
    if (argObjKey.isObject())
    {
        request.SetPrefix(argObjKey.keyA());
    }

    UINT64 commonPrefixTime = UINT64_MAX;
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
            traceW(L"fault: ListObjectsV2");

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
            // �^�C���X�^���v���̎�ł��Ȃ���ΎQ�ƃf�B���N�g���̂��̂��̗p

            commonPrefixTime = mRuntimeEnv->DefaultCommonPrefixTime;
        }

        // �f�B���N�g���̎��W

        for (const auto& it : result.GetCommonPrefixes())
        {
            const auto fullPath{ MB2WC(it.GetPrefix()) };
            //traceW(L"GetCommonPrefixes: %s", fullPath.c_str());

            // Prefix ��������菜��
            // 
            // "dir/"           --> ""
            // "dir/file1.txt"  --> "file1.txt"

            auto key{ fullPath.substr(argKeyLen) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                }
            }

            if (key.empty())
            {
                // �t�@�C��������("") �̂��̂̓f�B���N�g���E�I�u�W�F�N�g�Ƃ��Ĉ���

                key = L".";
            }

            APP_ASSERT(!key.empty());

            // �f�B���N�g���Ɠ����t�@�C�����͖������邽�߂ɕۑ�

            dirNames.insert(key);

            dirInfoList.push_back(makeDirInfoDir(key, commonPrefixTime));

            if (argLimit > 0)
            {
                if (dirInfoList.size() >= argLimit)
                {
                    goto exit;
                }
            }

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mRuntimeEnv->MaxDisplayObjects)
                {
                    traceW(L"warning: over max-objects(%d)", mRuntimeEnv->MaxDisplayObjects);

                    goto exit;
                }
            }
        }

        // �t�@�C���̎��W
        for (const auto& it : result.GetContents())
        {
            bool isDir = false;

            const auto fullPath{ MB2WC(it.GetKey()) };
            //traceW(L"GetContents: %s", fullPath.c_str());

            // Prefix ��������菜��
            // 
            // "dir/"           --> ""
            // "dir/file1.txt"  --> "file1.txt"

            auto key{ fullPath.substr(argKeyLen) };
            if (!key.empty())
            {
                if (key.back() == L'/')
                {
                    key.pop_back();
                    isDir = true;
                }
            }

            if (key.empty())
            {
                // �t�@�C��������("") �̂��̂̓f�B���N�g���E�I�u�W�F�N�g�Ƃ��Ĉ���

                isDir = true;
                key = L".";
            }

            APP_ASSERT(!key.empty());

            if (dirNames.find(key) != dirNames.end())
            {
                // �f�B���N�g���Ɠ������O�̃t�@�C���͖���

                continue;
            }

            auto dirInfo = makeEmptyDirInfo(key);
            APP_ASSERT(dirInfo);

            UINT32 FileAttributes = mRuntimeEnv->DefaultFileAttributes;

            if (key != L"." && key != L".." && key.at(0) == L'.')
            {
                // ".", ".." �ȊO�Ő擪�� "." �Ŏn�܂��Ă�����͉̂B���t�@�C���̈���

                FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            if (isDir)
            {
                FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            else
            {
                dirInfo->FileInfo.FileSize = it.GetSize();
                dirInfo->FileInfo.AllocationSize = (dirInfo->FileInfo.FileSize + ALLOCATION_UNIT - 1) / ALLOCATION_UNIT * ALLOCATION_UNIT;
            }

            dirInfo->FileInfo.FileAttributes |= FileAttributes;

            if (dirInfo->FileInfo.FileAttributes == 0)
            {
                dirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }

            const auto lastModified = UtcMillisToWinFileTime100ns(it.GetLastModified().Millis());

            dirInfo->FileInfo.CreationTime = lastModified;
            dirInfo->FileInfo.LastAccessTime = lastModified;
            dirInfo->FileInfo.LastWriteTime = lastModified;
            dirInfo->FileInfo.ChangeTime = lastModified;

            dirInfoList.emplace_back(dirInfo);

            if (argLimit > 0)
            {
                if (dirInfoList.size() >= argLimit)
                {
                    // ���ʃ��X�g�������Ŏw�肵�� limit �ɓ��B

                    goto exit;
                }
            }

            if (mRuntimeEnv->MaxDisplayObjects > 0)
            {
                if (dirInfoList.size() >= mRuntimeEnv->MaxDisplayObjects)
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
    *pDirInfoList = std::move(dirInfoList);

    return true;
}

bool ExecuteApi::DeleteObjects(CALLER_ARG
    const std::wstring& argBucket, const std::list<std::wstring>& argKeys)
{
    NEW_LOG_BLOCK();

    Aws::S3::Model::Delete delete_objects;

    for (const auto& it: argKeys)
    {
        Aws::S3::Model::ObjectIdentifier obj;
        obj.SetKey(WC2MB(it));
        delete_objects.AddObjects(obj);
    }

    traceW(L"DeleteObjects bucket=%s keys=%s", argBucket.c_str(), JoinStrings(argKeys, L',', false).c_str());

    Aws::S3::Model::DeleteObjectsRequest request;
    request.SetBucket(WC2MB(argBucket));
    request.SetDelete(delete_objects);

    const auto outcome = mS3Client->DeleteObjects(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObjects");
        return false;
    }

    return true;
}

bool ExecuteApi::DeleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    const auto outcome = mS3Client->DeleteObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    return true;
}

bool ExecuteApi::PutObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (FA_IS_DIRECTORY(argFileInfo.FileAttributes))
    {
        // �f�B���N�g���̏ꍇ�͋�̃R���e���c

        APP_ASSERT(!argSourcePath);
    }
    else
    {
        // �t�@�C���̏ꍇ�̓��[�J���E�L���b�V���̓��e���A�b�v���[�h����

        const Aws::String filePath{ WC2MB(argSourcePath) };

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>(
            __FUNCTION__,
            filePath.c_str(),
            std::ios_base::in | std::ios_base::binary
        );

        if (!inputData->good())
        {
            const auto lerr = ::GetLastError();

            traceW(L"fault: inputData->good, fail=%s bad=%s, eof=%s, lerr=%lu",
                BOOL_CSTRW(inputData->fail()), BOOL_CSTRW(inputData->bad()), BOOL_CSTRW(inputData->eof()), lerr);

            return false;
        }

        request.SetBody(inputData);
    }

    const auto creationTime{ std::to_string(argFileInfo.CreationTime) };
    const auto lastWriteTime{ std::to_string(argFileInfo.LastWriteTime) };
    const auto clientGuid{ WC2MB(mRuntimeEnv->ClientGuid) };

    request.AddMetadata("wincse-creation-time", creationTime.c_str());
    request.AddMetadata("wincse-last-write-time", lastWriteTime.c_str());
    request.AddMetadata("wincse-client-guid", clientGuid.c_str());

    traceA("creationTime=%s, lastWriteTime=%s, ClientGuid=%s",
        creationTime.c_str(), lastWriteTime.c_str(), clientGuid.c_str());

#if _DEBUG
    if (argSourcePath)
    {
        request.AddMetadata("wincse-debug-source-path", WC2MB(argSourcePath).c_str());
    }

    request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime).c_str());
    request.AddMetadata("wincse-debug-last-access-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastAccessTime).c_str());
#endif

    traceW(L"PutObject argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    const auto outcome = mS3Client->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    traceW(L"success");

    return true;
}

//
// GetObject() �Ŏ擾�������e���t�@�C���ɏo��
//
// argOffset)
//      -1 �ȉ�     �����o���I�t�Z�b�g�w��Ȃ�
//      ����ȊO    CreateFile ��� SetFilePointerEx �����s�����
//

static INT64 writeObjectResultToFile(CALLER_ARG
    const Aws::S3::Model::GetObjectResult& argResult, const FileOutputParams& argFOParams)
{
    NEW_LOG_BLOCK();

    traceW(argFOParams.str().c_str());

    // ���̓f�[�^
    const auto pbuf = argResult.GetBody().rdbuf();
    const auto inputSize = argResult.GetContentLength();  // �t�@�C���T�C�Y

    std::vector<char> vbuffer(1024 * 64);       // 64Kib

    // result �̓��e���t�@�C���ɏo�͂���

    auto remainingTotal = inputSize;

    FileHandle hFile = ::CreateFileW
    (
        argFOParams.mPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        argFOParams.mCreationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile.invalid())
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%ld", lerr);

        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argFOParams.mOffset;

    if (::SetFilePointerEx(hFile.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: SetFilePointerEx lerr=%ld", lerr);

        return -1LL;
    }

    while (remainingTotal > 0)
    {
        // �o�b�t�@�Ƀf�[�^��ǂݍ���

        char* buffer = vbuffer.data();
        const std::streamsize bytesRead = pbuf->sgetn(buffer, min(remainingTotal, (INT64)vbuffer.size()));
        if (bytesRead <= 0)
        {
            traceW(L"fault: Read error");

            return -1LL;
        }

        //traceW(L"%lld bytes read", bytesRead);

        // �t�@�C���Ƀf�[�^����������

        char* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            //traceW(L"%lld bytes remaining", remainingWrite);

            DWORD bytesWritten = 0;
            if (!::WriteFile(hFile.handle(), pos, (DWORD)remainingWrite, &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: WriteFile lerr=%ld", lerr);

                return -1LL;
            }

            //traceW(L"%lld bytes written", bytesWritten);

            pos += bytesWritten;
            remainingWrite -= bytesWritten;
        }

        remainingTotal -= bytesRead;
    }

    //traceW(L"return %lld", inputSize);

    return inputSize;
}

//
// �����Ŏw�肳�ꂽ���[�J���E�L���b�V�������݂��Ȃ��A���� �΂��� s3 �I�u�W�F�N�g��
// �X�V�������Â��ꍇ�͐V���� GetObject() �����s���ăL���b�V���E�t�@�C�����쐬����
// 
// argOffset)
//      -1 �ȉ�     �����o���I�t�Z�b�g�w��Ȃ�
//      ����ȊO    CreateFile ��� SetFilePointerEx �����s�����
//

INT64 ExecuteApi::GetObjectAndWriteToFile(CALLER_ARG
    const ObjectKey& argObjKey, const FileOutputParams& argFOParams)
{
    NEW_LOG_BLOCK();

    traceW(L"argObjKey=%s argFOParams=%s", argObjKey.c_str(), argFOParams.str().c_str());

    std::ostringstream ss;

    if (argFOParams.mLength > 0)
    {
        // mLength ���ݒ肳��Ă���Ƃ��̓}���`�p�[�g���̕����擾

        ss << "bytes=";
        ss << argFOParams.mOffset;
        ss << '-';
        ss << argFOParams.getOffsetEnd();
    }

    const std::string range{ ss.str() };
    //traceA("range=%s", range.c_str());

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (!range.empty())
    {
        request.SetRange(range);
    }

    const auto outcome = mS3Client->GetObject(request);
    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: GetObject");
        return -1LL;
    }

    const auto& result = outcome.GetResult();

    // result �̓��e���t�@�C���ɏo�͂���

    const auto bytesWritten = writeObjectResultToFile(CONT_CALLER result, argFOParams);

    if (bytesWritten < 0)
    {
        traceW(L"fault: writeObjectResultToFile");
        return -1LL;
    }

    return bytesWritten;
}

// EOF