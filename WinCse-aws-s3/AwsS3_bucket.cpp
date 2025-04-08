#include "AwsS3.hpp"

using namespace WCSE;


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


std::wstring AwsS3::getBucketLocation(CALLER_ARG const std::wstring& bucketName)
{
    //NEW_LOG_BLOCK();

    std::wstring bucketRegion{ mListBucketsCache.getBucketRegion(CONT_CALLER bucketName) };

    //traceW(L"bucketName: %s", bucketName.c_str());

    if (bucketRegion.empty())
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

        mListBucketsCache.addBucketRegion(CONT_CALLER bucketName, bucketRegion);
    }
    else
    {
        //traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }

    return bucketRegion;
}

bool AwsS3::unsafeHeadBucket(CALLER_ARG const std::wstring& argBucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    //traceW(L"bucket: %s", bucketName.c_str());

    if (!isInBucketFilters(argBucketName))
    {
        // �o�P�b�g�t�B���^�ɍ��v���Ȃ�

        traceW(L"%s: is not in filters, skip", argBucketName.c_str());

        return false;
    }

    // �L���b�V������T��

    const auto bucket{ mListBucketsCache.find(CONT_CALLER argBucketName) };
    if (!bucket)
    {
        // �L���b�V���Ɍ�����Ȃ�

        traceW(L"not found");
        return false;
    }

    const std::wstring bucketRegion{ this->getBucketLocation(CONT_CALLER argBucketName) };
    if (bucketRegion != mRegion)
    {
        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

        // ��\���ɂȂ�o�P�b�g�ɂ��� WinFsp �ɒʒm
        //getWorker(L"delayed")->addTask(START_CALLER new NotifRemoveBucketTask{ mFileSystem, std::wstring(L"\\") + argBucketName });

        return false;
    }

    if (pFileInfo)
    {
        const auto ntstatus = GetFileInfoInternal(mRefDir.handle(), pFileInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: GetFileInfoInternal");
            return false;
        }
    }

    //traceW(L"success");

    return true;
}

bool AwsS3::unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */, const std::vector<std::wstring>& options)
{
    NEW_LOG_BLOCK();

    DirInfoListType dirInfoList;

    if (mListBucketsCache.empty(CONT_CALLER0))
    {
        const auto now{ std::chrono::system_clock::now() };
        const auto lastSetTime{ mListBucketsCache.getLastSetTime(CONT_CALLER0) };
        const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastSetTime);

        if (elapsed.count() < mConfig.bucketCacheExpiryMin)
        {
            // �o�P�b�g�ꗗ����ł���󋵂̃L���b�V���L��������

            traceW(L"empty buckets, short time cache");
            return true;
        }

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

            // �o�P�b�g�̍쐬�������擾

            const auto creationMillis{ bucket.GetCreationDate().Millis() };
            traceW(L"bucketName=%s, CreationDate=%s", bucketName.c_str(), UtcMilliToLocalTimeStringW(creationMillis).c_str());

            const auto FileTime = UtcMillisToWinFileTime100ns(creationMillis);

            // �f�B���N�g���E�G���g���𐶐�

            auto dirInfo = makeDirInfo_dir(bucketName, FileTime);
            APP_ASSERT(dirInfo);

            // �o�P�b�g�͏�ɓǂݎ���p

            dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;

            dirInfoList.emplace_back(dirInfo);

            // �ő�o�P�b�g�\�����̊m�F

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

        mListBucketsCache.set(CONT_CALLER dirInfoList);
    }
    else
    {
        // �L���b�V������R�s�[

        dirInfoList = mListBucketsCache.get(CONT_CALLER0);

        //traceW(L"use cache: size=%zu", dirInfoList.size());
    }

    if (pDirInfoList)
    {
        for (auto it=dirInfoList.begin(); it!=dirInfoList.end(); )
        {
            const std::wstring bucketName{ (*it)->FileNameBuf };

            // ���[�W�����E�L���b�V������قȂ郊�[�W�����ł��邩���ׂ�

            const std::wstring bucketRegion{ mListBucketsCache.getBucketRegion(CONT_CALLER bucketName) };

            if (!bucketRegion.empty())
            {
                if (bucketRegion != mRegion)
                {
                    // ���[�W�������قȂ�ꍇ�� HIDDEN ������t�^
                    //
                    // --> headBucket() �Ń��[�W�������擾���Ă���̂ŁA�o�P�b�g�E�L���b�V���쐬���ł͂ł��Ȃ�

                    (*it)->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }
            }

            if (!options.empty())
            {
                const auto itOpts{ std::find(options.begin(), options.end(), bucketName) };
                if (itOpts == options.end())
                {
                    // �������o�����Ɉ�v���Ȃ��ꍇ�͎�菜��

                    it = dirInfoList.erase(it);
                    continue;
                }
            }

            ++it;
        }

        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// -----------------------------------------------------------------------------------
//
// �O������Ăяo�����C���^�[�t�F�[�X
//

void AwsS3::clearListBucketsCache(CALLER_ARG0)
{
    mListBucketsCache.clear(CONT_CALLER0);
}

void AwsS3::reportListBucketsCache(CALLER_ARG FILE* fp)
{
    mListBucketsCache.report(CONT_CALLER fp);
}

//
// �������牺�̃��\�b�h�� THREAD_SAFE �}�N���ɂ��C�����K�v
//

static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }


bool AwsS3::headBucket(CALLER_ARG const std::wstring& argBucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headBucket);
    THREAD_SAFE();

    return this->unsafeHeadBucket(CONT_CALLER argBucketName, pFileInfo);
}

bool AwsS3::listBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(listBuckets);
    THREAD_SAFE();

    return this->unsafeListBuckets(CONT_CALLER pDirInfoList, {});
}

bool AwsS3::reloadListBucketsCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    const auto lastSetTime = mListBucketsCache.getLastSetTime(CONT_CALLER0);

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

        mListBucketsCache.clear(CONT_CALLER0);

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