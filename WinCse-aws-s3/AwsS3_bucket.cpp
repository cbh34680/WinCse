#include "AwsS3.hpp"
#include "BucketCache.hpp"


using namespace WinCseLib;


struct NotifRemoveBucketTask : public IOnDemandTask
{
    IgnoreDuplicates getIgnoreDuplicates() const noexcept override { return IgnoreDuplicates::Yes; }
    Priority getPriority() const noexcept override { return Priority::Low; }

    FSP_FILE_SYSTEM* mFileSystem;
    const std::wstring mFileName;

    NotifRemoveBucketTask(FSP_FILE_SYSTEM* argFileSystem, const std::wstring& argFileName)
        : mFileSystem(argFileSystem), mFileName(argFileName) { }

    std::wstring synonymString() const noexcept override
    {
        return std::wstring(L"NotifyRemoveBucketTask; ") + mFileName;
    }

    void run(CALLER_ARG0) override
    {
        NEW_LOG_BLOCK();

        traceW(L"exec FspFileSystemNotify**");

        NTSTATUS ntstatus = FspFileSystemNotifyBegin(mFileSystem, 1000UL);
        if (NT_SUCCESS(ntstatus))
        {
            union
            {
                FSP_FSCTL_NOTIFY_INFO V;
                UINT8 B[1024];
            } Buffer{};
            ULONG Length = 0;
            union
            {
                FSP_FSCTL_NOTIFY_INFO V;
                UINT8 B[sizeof(FSP_FSCTL_NOTIFY_INFO) + MAX_PATH * sizeof(WCHAR)];
            } NotifyInfo{};

            NotifyInfo.V.Size = (UINT16)(sizeof(FSP_FSCTL_NOTIFY_INFO) + mFileName.length() * sizeof(WCHAR));
            NotifyInfo.V.Filter = FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_ACTION_MODIFIED;
            NotifyInfo.V.Action = FILE_ACTION_REMOVED;
            memcpy(NotifyInfo.V.FileNameBuf, mFileName.c_str(), NotifyInfo.V.Size - sizeof(FSP_FSCTL_NOTIFY_INFO));

            FspFileSystemAddNotifyInfo(&NotifyInfo.V, &Buffer, sizeof Buffer, &Length);

            ntstatus = FspFileSystemNotify(mFileSystem, &Buffer.V, Length);
            //APP_ASSERT(STATUS_SUCCESS == ntstatus);

            ntstatus = FspFileSystemNotifyEnd(mFileSystem);
            //APP_ASSERT(STATUS_SUCCESS == ntstatus);
        }
    }
};


static BucketCache gBucketCache;

std::wstring AwsS3::unsafeGetBucketRegion(CALLER_ARG const std::wstring& bucketName)
{
    //NEW_LOG_BLOCK();

    std::wstring bucketRegion;

    //traceW(L"bucketName: %s", bucketName.c_str());

    if (gBucketCache.findRegion(CONT_CALLER bucketName, &bucketRegion))
    {
        //traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }
    else
    {
        // �L���b�V���ɑ��݂��Ȃ�

        //traceW(L"do GetBucketLocation()");

        namespace mapper = Aws::S3::Model::BucketLocationConstraintMapper;

        Aws::S3::Model::GetBucketLocationRequest request;
        request.SetBucket(WC2MB(bucketName));

        const auto outcome = mClient->GetBucketLocation(request);
        if (outcomeIsSuccess(outcome))
        {
            // ���P�[�V�������擾�ł����Ƃ�

            const auto& result = outcome.GetResult();
            const auto& location = result.GetLocationConstraint();

            bucketRegion = MB2WC(mapper::GetNameForBucketLocationConstraint(location));

            //traceW(L"success, region is %s", bucketRegion.c_str());
        }
        
        if (bucketRegion.empty())
        {
            // �擾�ł��Ȃ��Ƃ����܂߂āA�l���Ȃ��Ƃ��̓f�t�H���g�l�ɂ���

            bucketRegion = MB2WC(AWS_DEFAULT_REGION);

            //traceW(L"error, fall back region is %s", bucketRegion.c_str());
        }

        gBucketCache.updateRegion(CONT_CALLER bucketName, bucketRegion);
    }

    return bucketRegion;
}

bool AwsS3::unsafeHeadBucket(CALLER_ARG const std::wstring& bucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!bucketName.empty());
    APP_ASSERT(bucketName.back() != L'/');

    //traceW(L"bucket: %s", bucketName.c_str());

    if (!isInBucketFilters(bucketName))
    {
        // �o�P�b�g�t�B���^�ɍ��v���Ȃ�
        traceW(L"%s: is not in filters, skip", bucketName.c_str());

        return false;
    }

    // �L���b�V������T��
    const auto bucket{ gBucketCache.find(CONT_CALLER bucketName) };
    if (bucket)
    {
        //traceW(L"hit in buckets cache");
    }
    else
    {
        //traceW(L"warn: no match");

        Aws::S3::Model::HeadBucketRequest request;
        request.SetBucket(WC2MB(bucketName));

        const auto outcome = mClient->HeadBucket(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: HeadBucket");
            return false;
        }
    }

    const std::wstring bucketRegion{ this->unsafeGetBucketRegion(CONT_CALLER bucketName) };
    if (bucketRegion != mRegion)
    {
        // �o�P�b�g�̃��[�W�������قȂ�̂ŋ���

        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

        // ��\���ɂȂ�o�P�b�g�ɂ��� WinFsp �ɒʒm

        getWorker(L"delayed")->addTask(START_CALLER new NotifRemoveBucketTask{ mFileSystem, std::wstring(L"\\") + bucketName });

        return false;
    }

    if (pFileInfo)
    {
        NTSTATUS ntstatus = GetFileInfoInternal(mRefDir.handle(), pFileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return false;
        }
    }

    //traceW(L"success");

    return true;
}

bool AwsS3::unsafeListBuckets(CALLER_ARG DirInfoListType* pDirInfoList /* nullable */, const std::vector<std::wstring>& options)
{
    NEW_LOG_BLOCK();

    DirInfoListType dirInfoList;

    if (gBucketCache.empty(CONT_CALLER0))
    {
        //traceW(L"cache empty");

        // �o�P�b�g�ꗗ�̎擾

        Aws::S3::Model::ListBucketsRequest request;

        const auto outcome = mClient->ListBuckets(request);
        if (!outcomeIsSuccess(outcome))
        {
            traceW(L"fault: ListBuckets");
            return false;
        }

        const auto& result = outcome.GetResult();

        for (const auto& bucket : result.GetBuckets())
        {
            const auto bucketName{ MB2WC(bucket.GetName()) };

            if (!isInBucketFilters(bucketName))
            {
                // �o�P�b�g���ɂ��t�B���^�����O

                //traceW(L"%s: is not in filters, skip", bucketName.c_str());
                continue;
            }

            std::wstring bucketRegion;
            if (gBucketCache.findRegion(CONT_CALLER bucketName, &bucketRegion))
            {
                // �قȂ郊�[�W�����̃o�P�b�g�͖���

                if (bucketRegion != mRegion)
                {
                    //traceW(L"%s: no match region, skip", bucketRegion.c_str());
                    continue;
                }
            }

            const auto creationMillis{ bucket.GetCreationDate().Millis() };
            traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMilliToLocalTimeStringW(creationMillis).c_str());

            const auto FileTime = UtcMillisToWinFileTime100ns(creationMillis);

            auto dirInfo = makeDirInfo_dir(bucketName, FileTime);
            APP_ASSERT(dirInfo);

            // �o�P�b�g�͏�ɓǂݎ���p

            dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;

            dirInfoList.emplace_back(dirInfo);

            if (mConfig.maxDisplayBuckets > 0)
            {
                if (dirInfoList.size() >= mConfig.maxDisplayBuckets)
                {
                    break;
                }
            }
        }

        //traceW(L"update cache");

        // �L���b�V���ɃR�s�[
        gBucketCache.save(CONT_CALLER dirInfoList);
    }
    else
    {
        // �L���b�V������R�s�[
        gBucketCache.load(CONT_CALLER mRegion, dirInfoList);

        //traceW(L"use cache: size=%zu", dirInfoList.size());
    }

    bool ret = false;

    if (pDirInfoList)
    {
        if (options.empty())
        {
            // ���o�������Ȃ��̂ŁA�S�Ē�

            *pDirInfoList = std::move(dirInfoList);
            ret = true;
        }
        else
        {
            // ���o�����Ɉ�v������̂��

            DirInfoListType optsDirInfoList;

            for (const auto& dirInfo : dirInfoList)
            {
                const std::wstring bucketName{ dirInfo->FileNameBuf };
                const auto it = std::find(options.begin(), options.end(), bucketName);

                if (it != options.end())
                {
                    optsDirInfoList.push_back(dirInfo);
                }

                if (optsDirInfoList.size() == options.size())
                {
                    break;
                }
            }

            if (!optsDirInfoList.empty())
            {
                *pDirInfoList = std::move(optsDirInfoList);
                ret = true;
            }
        }
    }
    else
    {
        ret = !dirInfoList.empty();
    }

    return ret;
}

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//

static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool AwsS3::headBucket(CALLER_ARG const std::wstring& bucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headBucket);
    THREAD_SAFE();

    return this->unsafeHeadBucket(CONT_CALLER bucketName, pFileInfo);
}

bool AwsS3::listBuckets(CALLER_ARG DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(listBuckets);
    THREAD_SAFE();

    return this->unsafeListBuckets(CONT_CALLER pDirInfoList, {});
}

DirInfoType AwsS3::getBucket(CALLER_ARG const std::wstring& bucketName)
{
    StatsIncr(getBucket);
    THREAD_SAFE();

    DirInfoListType dirInfoList;

    // ���O���w�肵�ă��X�g���擾

    if (!this->unsafeListBuckets(CONT_CALLER &dirInfoList, { bucketName }))
    {
        return nullptr;
    }

    APP_ASSERT(dirInfoList.size() == 1);

    return *dirInfoList.begin();
}

void AwsS3::clearBucketCache(CALLER_ARG0)
{
    THREAD_SAFE();

    gBucketCache.clear(CONT_CALLER0);
}

void AwsS3::reportBucketCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();

    gBucketCache.report(CONT_CALLER fp);
}

bool AwsS3::reloadBucketCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();
    //NEW_LOG_BLOCK();

    const auto lastSetTime = gBucketCache.getLastSetTime(CONT_CALLER0);

    if (threshold < lastSetTime)
    {
        // �L���b�V���̗L��������

        //traceW(L"bucket cache is valid");
    }
    else
    {
        NEW_LOG_BLOCK();

        // �o�P�b�g�E�L���b�V�����쐬���Ă����莞�Ԃ��o��

        traceW(L"RELOAD");

        // �o�P�b�g�̃L���b�V�����폜���āA�ēx�ꗗ���擾����

        gBucketCache.clear(CONT_CALLER0);

        // �o�P�b�g�ꗗ�̎擾 --> �L���b�V���̐���

        if (!this->unsafeListBuckets(CONT_CALLER nullptr, {}))
        {
            traceW(L"fault: listBuckets");
            return false;
        }
    }

    return true;
}

// EOF