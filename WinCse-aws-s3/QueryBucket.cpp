#include "QueryBucket.hpp"

using namespace WCSE;


#define REMOVE_BUCKET_OTHER_REGION      (0)

#if REMOVE_BUCKET_OTHER_REGION
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
#endif


void QueryBucket::clearListBucketsCache(CALLER_ARG0)
{
    mCacheListBuckets.clear(CONT_CALLER0);
}

void QueryBucket::reportListBucketsCache(CALLER_ARG FILE* fp)
{
    mCacheListBuckets.report(CONT_CALLER fp);
}

std::wstring QueryBucket::unsafeGetBucketRegion(CALLER_ARG const std::wstring& argBucketName)
{
    //NEW_LOG_BLOCK();

    //traceW(L"bucketName: %s", bucketName.c_str());

    std::wstring bucketRegion;

    if (mCacheListBuckets.getBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
    {
        APP_ASSERT(!bucketRegion.empty());

        //traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }
    else
    {
        // �L���b�V���ɑ��݂��Ȃ�

        if (!mExecuteApi->GetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
        {
            // �擾�ł��Ȃ��Ƃ��̓f�t�H���g�l�ɂ���

            bucketRegion = MB2WC(AWS_DEFAULT_REGION);

            //traceW(L"error, fall back region is %s", bucketRegion.c_str());
        }

        APP_ASSERT(!bucketRegion.empty());

        mCacheListBuckets.addBucketRegion(CONT_CALLER argBucketName, bucketRegion);
    }

    return bucketRegion;
}

DirInfoType QueryBucket::unsafeHeadBucket(CALLER_ARG const std::wstring& argBucketName)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    //traceW(L"bucket: %s", bucketName.c_str());

    const auto bucketRegion{ this->unsafeGetBucketRegion(CONT_CALLER argBucketName) };
    if (bucketRegion != mRuntimeEnv->Region)
    {
        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

#if REMOVE_BUCKET_OTHER_REGION
        // ��\���ɂȂ�o�P�b�g�ɂ��� WinFsp �ɒʒm
        getWorker(L"delayed")->addTask(START_CALLER new NotifRemoveBucketTask{ mFileSystem, std::wstring(L"\\") + argBucketName });
#endif

        return nullptr;
    }

    // �L���b�V������T��

    return mCacheListBuckets.find(CONT_CALLER argBucketName);
}

bool QueryBucket::unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */, const std::vector<std::wstring>& options)
{
    NEW_LOG_BLOCK();

    DirInfoListType dirInfoList;

    if (mCacheListBuckets.empty(CONT_CALLER0))
    {
        const auto now{ std::chrono::system_clock::now() };
        const auto lastSetTime{ mCacheListBuckets.getLastSetTime(CONT_CALLER0) };
        const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastSetTime);

        if (elapsed.count() < mRuntimeEnv->BucketCacheExpiryMin)
        {
            // �o�P�b�g�ꗗ����ł���󋵂̃L���b�V���L��������

            traceW(L"empty buckets, short time cache");
            return true;
        }

        //traceW(L"cache empty");

        // �o�P�b�g�ꗗ�̎擾

        if (!mExecuteApi->ListBuckets(CONT_CALLER &dirInfoList))
        {
            traceW(L"fault: ListBuckets");
            return false;
        }

        //traceW(L"update cache");

        // �L���b�V���ɃR�s�[

        mCacheListBuckets.set(CONT_CALLER dirInfoList);
    }
    else
    {
        // �L���b�V������R�s�[

        mCacheListBuckets.get(CONT_CALLER0, &dirInfoList);

        //traceW(L"use cache: size=%zu", dirInfoList.size());
    }

    if (pDirInfoList)
    {
        for (auto it=dirInfoList.begin(); it!=dirInfoList.end(); )
        {
            const std::wstring bucketName{ (*it)->FileNameBuf };

            // ���[�W�����E�L���b�V������擾

            std::wstring bucketRegion;

            if (mCacheListBuckets.getBucketRegion(CONT_CALLER bucketName, &bucketRegion))
            {
                APP_ASSERT(!bucketRegion.empty());

                // �قȂ郊�[�W�����ł��邩���ׂ�

                if (bucketRegion != mRuntimeEnv->Region)
                {
                    // ���[�W�������قȂ�ꍇ�� HIDDEN ������t�^
                    //
                    // --> headBucket() �Ń��[�W�������擾���Ă���̂ŁA�o�P�b�g�E�L���b�V���쐬���ł͂ł��Ȃ�

                    (*it)->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }
            }

            if (!options.empty())
            {
                const auto itOpts{ std::find(options.cbegin(), options.cend(), bucketName) };
                if (itOpts == options.cend())
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

bool QueryBucket::unsafeReloadListBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    const auto lastSetTime = mCacheListBuckets.getLastSetTime(CONT_CALLER0);

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

        mCacheListBuckets.clear(CONT_CALLER0);

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