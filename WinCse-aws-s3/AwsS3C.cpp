#include "AwsS3C.hpp"

#define REMOVE_BUCKET_OTHER_REGION      (0)

using namespace WCSE;


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

static PCWSTR CACHE_DATA_DIR_FNAME = L"aws-s3\\cache\\data";
static PCWSTR CACHE_REPORT_DIR_FNAME = L"aws-s3\\cache\\report";


NTSTATUS AwsS3C::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
	NEW_LOG_BLOCK();

	const auto ntstatus = AwsS3A::OnSvcStart(argWorkDir, FileSystem);
	if (!NT_SUCCESS(ntstatus))
	{
		traceW(L"fault: AwsS3A::OnSvcStart");
		return ntstatus;
	}

    // �t�@�C���E�L���b�V���ۑ��p�f�B���N�g���̏���

    const auto cacheDataDir{ mWorkDir + L'\\' + CACHE_DATA_DIR_FNAME };
    if (!MkdirIfNotExists(cacheDataDir))
    {
        traceW(L"%s: can not create directory", cacheDataDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    const auto cacheReportDir{ mWorkDir + L'\\' + CACHE_REPORT_DIR_FNAME };
    if (!MkdirIfNotExists(cacheReportDir))
    {
        traceW(L"%s: can not create directory", cacheReportDir.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#ifdef _DEBUG
    forEachFiles(cacheDataDir, [this, &LOG_BLOCK()](const auto& wfd, const auto& fullPath)
    {
        APP_ASSERT(!FA_IS_DIRECTORY(wfd.dwFileAttributes));

        traceW(L"cache file: [%s]", fullPath.c_str());
    });
#endif

    // �����o�ɕۑ�

    mCacheDataDir = cacheDataDir;
    mCacheReportDir = cacheReportDir;

	return STATUS_SUCCESS;
}

VOID AwsS3C::OnSvcStop()
{
    // �f�X�g���N�^������Ă΂��̂ŁA�ē��\�Ƃ��Ă�������

    AwsS3A::OnSvcStop();
}

void AwsS3C::clearListBucketsCache(CALLER_ARG0)
{
    mListBucketsCache.clear(CONT_CALLER0);
}

void AwsS3C::reportListBucketsCache(CALLER_ARG FILE* fp)
{
    mListBucketsCache.report(CONT_CALLER fp);
}

DirInfoType AwsS3C::getCachedHeadObject(CALLER_ARG const ObjectKey& argObjKey)
{
    DirInfoType dirInfo;

    if (mHeadObjectCache.get(CONT_CALLER argObjKey, &dirInfo))
    {
        return dirInfo;
    }

    return nullptr;
}

bool AwsS3C::isNegativeHeadObject(CALLER_ARG const ObjectKey& argObjKey)
{
    return mHeadObjectCache.isNegative(CONT_CALLER argObjKey); 
}

void AwsS3C::reportObjectCache(CALLER_ARG FILE* fp)
{
    mHeadObjectCache.report(CONT_CALLER fp);
    mListObjectsCache.report(CONT_CALLER fp);
}

int AwsS3C::deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    const auto delHead = mHeadObjectCache.deleteByTime(CONT_CALLER threshold);
    const auto delList = mListObjectsCache.deleteByTime(CONT_CALLER threshold);

    return delHead + delList;
}

int AwsS3C::clearObjectCache(CALLER_ARG0)
{
    const auto now{ std::chrono::system_clock::now() };

    return this->deleteOldObjectCache(CONT_CALLER now);
}

int AwsS3C::deleteObjectCache(CALLER_ARG const ObjectKey& argObjKey)
{
    const auto delHead = mHeadObjectCache.deleteByKey(CONT_CALLER argObjKey);
    const auto delList = mListObjectsCache.deleteByKey(CONT_CALLER argObjKey);

    return delHead + delList;
}

std::wstring AwsS3C::unsafeGetBucketRegion(CALLER_ARG const std::wstring& argBucketName)
{
    //NEW_LOG_BLOCK();

    //traceW(L"bucketName: %s", bucketName.c_str());

    std::wstring bucketRegion;

    if (mListBucketsCache.getBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
    {
        APP_ASSERT(!bucketRegion.empty());

        //traceW(L"hit in cache, region is %s", bucketRegion.c_str());
    }
    else
    {
        // �L���b�V���ɑ��݂��Ȃ�

        if (!this->apicallGetBucketRegion(CONT_CALLER argBucketName, &bucketRegion))
        {
            // �擾�ł��Ȃ��Ƃ��̓f�t�H���g�l�ɂ���

            bucketRegion = MB2WC(AWS_DEFAULT_REGION);

            //traceW(L"error, fall back region is %s", bucketRegion.c_str());
        }

        mListBucketsCache.addBucketRegion(CONT_CALLER argBucketName, bucketRegion);
    }

    return bucketRegion;
}

bool AwsS3C::unsafeHeadBucket(CALLER_ARG const std::wstring& argBucketName, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucketName.empty());
    APP_ASSERT(argBucketName.back() != L'/');

    //traceW(L"bucket: %s", bucketName.c_str());

    if (!this->isInBucketFilters(argBucketName))
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

    const auto bucketRegion{ this->unsafeGetBucketRegion(CONT_CALLER argBucketName) };
    if (bucketRegion != this->getClientRegion())
    {
        traceW(L"%s: no match bucket-region", bucketRegion.c_str());

#if REMOVE_BUCKET_OTHER_REGION
        // ��\���ɂȂ�o�P�b�g�ɂ��� WinFsp �ɒʒm
        //getWorker(L"delayed")->addTask(START_CALLER new NotifRemoveBucketTask{ mFileSystem, std::wstring(L"\\") + argBucketName });
#endif

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

bool AwsS3C::unsafeListBuckets(CALLER_ARG WCSE::DirInfoListType* pDirInfoList /* nullable */, const std::vector<std::wstring>& options)
{
    NEW_LOG_BLOCK();

    DirInfoListType dirInfoList;

    if (mListBucketsCache.empty(CONT_CALLER0))
    {
        const auto now{ std::chrono::system_clock::now() };
        const auto lastSetTime{ mListBucketsCache.getLastSetTime(CONT_CALLER0) };
        const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastSetTime);

        if (elapsed.count() < mSettings->bucketCacheExpiryMin)
        {
            // �o�P�b�g�ꗗ����ł���󋵂̃L���b�V���L��������

            traceW(L"empty buckets, short time cache");
            return true;
        }

        //traceW(L"cache empty");

        // �o�P�b�g�ꗗ�̎擾

        if (!this->apicallListBuckets(CONT_CALLER &dirInfoList))
        {
            traceW(L"fault: apicallListBuckets");
            return false;
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

            // ���[�W�����E�L���b�V������擾

            std::wstring bucketRegion;

            if (mListBucketsCache.getBucketRegion(CONT_CALLER bucketName, &bucketRegion))
            {
                APP_ASSERT(!bucketRegion.empty());

                // �قȂ郊�[�W�����ł��邩���ׂ�

                if (bucketRegion != this->getClientRegion())
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

bool AwsS3C::unsafeReloadListBuckets(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
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

DirInfoType AwsS3C::unsafeHeadObjectWithCache(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(!argObjKey.isBucket());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mHeadObjectCache.isNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return nullptr;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoType dirInfo;

    if (mHeadObjectCache.get(CONT_CALLER argObjKey, &dirInfo))
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
    }
    else
    {
        // HeadObject API �̎��s

        if (!this->apicallHeadObject(CONT_CALLER argObjKey, &dirInfo))
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            NEW_LOG_BLOCK();

            traceW(L"fault: apicallHeadObject");

            mHeadObjectCache.addNegative(CONT_CALLER argObjKey);

            return nullptr;
        }

        // �L���b�V���ɃR�s�[

        NEW_LOG_BLOCK();

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mHeadObjectCache.set(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    return dirInfo;
}

DirInfoType AwsS3C::unsafeHeadObjectWithCache_CheckDir(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(!argObjKey.isBucket());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mHeadObjectCache.isNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return nullptr;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoType dirInfo;

    if (mHeadObjectCache.get(CONT_CALLER argObjKey, &dirInfo))
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�������
    }
    else
    {
        NEW_LOG_BLOCK();

        // �e�f�B���N�g���� CommonPrefix ����f�B���N�g�������擾

        std::wstring parentDir;
        std::wstring searchName;

        const auto b = SplitPath(argObjKey.str(), &parentDir, &searchName);
        APP_ASSERT(b);

        DirInfoListType dirInfoList;

        if (!this->apicallListObjectsV2(CONT_CALLER ObjectKey::fromPath(parentDir), true, 0, &dirInfoList))
        {
            // �G���[�̎��̓l�K�e�B�u�E�L���b�V���ɓo�^

            traceW(L"fault: apicallListObjectsV2");

            mHeadObjectCache.addNegative(CONT_CALLER argObjKey);

            return nullptr;
        }

        // �e�f�B���N�g���̃��X�g���疼�O�̈�v������̂�T��

        for (const auto& it: dirInfoList)
        {
            std::wstring fileName{ it->FileNameBuf };

            if (FA_IS_DIRECTORY(it->FileInfo.FileAttributes))
            {
                // FileNameBuf �̓��e�� L"dirname" �Ȃ̂ŁAL"dirname/" �ɒ���

                fileName += L'/';
            }

            if (fileName == searchName)
            {
                dirInfo = makeDirInfo_dir(it->FileNameBuf, it->FileInfo.LastWriteTime);
                break;
            }
        }

        if (!dirInfo)
        {
            // ������Ȃ�������l�K�e�B�u�E�L���b�V���ɓo�^

            traceW(L"not found in Parent CommonPrefix");

            mHeadObjectCache.addNegative(CONT_CALLER argObjKey);

            return nullptr;
        }

        // �L���b�V���ɃR�s�[

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mHeadObjectCache.set(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    return dirInfo;
}

bool AwsS3C::unsafeListObjectsWithCache(CALLER_ARG const ObjectKey& argObjKey,
    DirInfoListType* pDirInfoList /* nullable */)
{
    APP_ASSERT(argObjKey.meansDir());

    // �l�K�e�B�u�E�L���b�V���𒲂ׂ�

    if (mListObjectsCache.isNegative(CONT_CALLER argObjKey))
    {
        // �l�K�e�B�u�E�L���b�V�����Ɍ�������

        return false;
    }

    // �|�W�e�B�u�E�L���b�V���𒲂ׂ�

    DirInfoListType dirInfoList;

    if (mListObjectsCache.get(CONT_CALLER argObjKey, &dirInfoList))
    {
        // �|�W�e�B�u�E�L���b�V���Ɍ�������
    }
    else
    {
        // �|�W�e�B�u�E�L���b�V�����Ɍ�����Ȃ�

        if (!this->apicallListObjectsV2(CONT_CALLER argObjKey, true, 0, &dirInfoList))
        {
            // �l�K�e�B�u�E�L���b�V���ɓo�^

            NEW_LOG_BLOCK();
            traceW(L"fault: apicallListObjectsV2");

            mListObjectsCache.addNegative(CONT_CALLER argObjKey);

            return false;
        }

        // �|�W�e�B�u�E�L���b�V���ɃR�s�[

        NEW_LOG_BLOCK();
        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mListObjectsCache.set(CONT_CALLER argObjKey, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}


// EOF