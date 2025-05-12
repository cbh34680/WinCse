#include "CSDevice.hpp"

using namespace CSELIB;
using namespace CSESS3;


CSDevice::~CSDevice()
{
    this->OnSvcStop();
}

struct ListBucketsTask : public IOnDemandTask
{
    CSDevice* mThat;

    ListBucketsTask(CSDevice* argThat)
        :
        mThat(argThat)
    {
    }

    void run(int) override
    {
        NEW_LOG_BLOCK();

        //traceW(L"call ListBuckets");

        mThat->listBuckets(START_CALLER nullptr);
    }
};

NTSTATUS CSDevice::OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem)
{
    NEW_LOG_BLOCK();

    const auto ntstatus = CSDeviceBase::OnSvcStart(argWorkDir, FileSystem);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: AwsS3A::OnSvcStart");
        return ntstatus;
    }

    // �o�P�b�g�ꗗ�̐�ǂ�

    getWorker(L"delayed")->addTask(new ListBucketsTask{ this });

    return STATUS_SUCCESS;
}

bool CSDevice::headBucket(CALLER_ARG const std::wstring& argBucketName, DirEntryType* pDirEntry)
{
    return mQueryBucket->qbHeadBucket(CONT_CALLER argBucketName, pDirEntry);
}

bool CSDevice::listBuckets(CALLER_ARG DirEntryListType* pDirEntryList)
{
    return mQueryBucket->qbListBuckets(CONT_CALLER pDirEntryList);
}

bool CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    APP_ASSERT(argObjKey.isObject());

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂ŁA��̃f�B���N�g���E�I�u�W�F�N�g�����݂��Ȃ��Ƃ���
    // ListObjects() �����s���āA���W�b�N�Ŕ��f���邱�ƂɂȂ�B
    // ����ł͗��p���鑤���Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    if (argObjKey.meansDir())
    {
        return mQueryObject->qoHeadObjectOrListObjects(CONT_CALLER argObjKey, pDirEntry);
    }
    else
    {
        APP_ASSERT(argObjKey.meansFile());

        return mQueryObject->qoHeadObject(CONT_CALLER argObjKey, pDirEntry);
    }
}

bool CSDevice::headObjectFromCache_(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    // listDisplayObjects �̒��ł̂ݗ��p�����֐�

    if (mRuntimeEnv->StrictFileTimestamp)
    {
        // HeadObject ���擾

        return this->headObject(CONT_CALLER argObjKey, pDirEntry);
    }
    else
    {
        // HeadObject �̃L���b�V��������

        return mQueryObject->qoHeadObjectFromCache(CONT_CALLER argObjKey, pDirEntry);
    }
}

bool CSDevice::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
    APP_ASSERT(argObjKey.meansDir());
    APP_ASSERT(pDirEntryList);

    DirEntryListType dirEntryList;

    if (!this->listObjects(CONT_CALLER argObjKey, &dirEntryList))
    {
        NEW_LOG_BLOCK();
        errorW(L"fault: listObjects");

        return false;
    }

    // dirEntryList �̓��e�� HeadObject �Ŏ擾�����L���b�V���ƃ}�[�W

    for (auto& dirEntry: dirEntryList)
    {
        APP_ASSERT(dirEntry->mName != L"." && dirEntry->mName != L"..");

        // �f�B���N�g���Ƀt�@�C������t�^

        const auto searchObjKey{ argObjKey.append(dirEntry->mName) };

        APP_ASSERT(searchObjKey.isObject());

        DirEntryType mergeDirEntry;

        if (this->headObjectFromCache_(CONT_CALLER searchObjKey, &mergeDirEntry))
        {
            // �L���b�V������擾�o�����獷���ւ�

            dirEntry = std::move(mergeDirEntry);
        }

        if (mQueryObject->qoIsInNegativeCache(CONT_CALLER searchObjKey))
        {
            // ���[�W�����Ⴂ�Ȃǂ� HeadObject �����s�������̂� HIDDEN ������ǉ�

            dirEntry->mFileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
        }
    }

    // �h�b�g�G���g���̒ǉ� (CMD �Ή�)

    const auto it = std::min_element(dirEntryList.cbegin(), dirEntryList.cend(), [](const auto& l, const auto& r)
    {
        return l->mFileInfo.LastWriteTime < r->mFileInfo.LastWriteTime;
    });

    const FILETIME_100NS_T defaultFileTime = it == dirEntryList.cend()
        ? mRuntimeEnv->DefaultCommonPrefixTime : (*it)->mFileInfo.LastWriteTime;

    if (!argObjKey.isBucket())
    {
        // CMD �̓���ɍ��킹��
        //      C:\             ... ".", ".." �͕\������Ȃ�
        //      C:\dir          ... "." �͕\�������
        //      C:\dir\subdir   ... ".", ".." ���\�������

        DirEntryType dirEntry;

        const auto optParentDir{ argObjKey.toParentDir() };
        if (optParentDir)
        {
            if (optParentDir->isBucket())
            {
                this->headBucket(CONT_CALLER optParentDir->bucket(), &dirEntry);
            }
            else
            {
                this->headObjectFromCache_(CONT_CALLER *optParentDir, &dirEntry);
            }
        }

        const FILETIME_100NS_T fileTime = dirEntry ? dirEntry->mFileInfo.LastWriteTime : defaultFileTime;

        dirEntryList.push_front(DirectoryEntry::makeDotEntry(L"..", fileTime));
    }

    DirEntryType dirEntry;

    if (argObjKey.isBucket())
    {
        this->headBucket(CONT_CALLER argObjKey.bucket(), &dirEntry);
    }
    else
    {
        this->headObjectFromCache_(CONT_CALLER argObjKey, &dirEntry);
    }

    const FILETIME_100NS_T fileTime = dirEntry ? dirEntry->mFileInfo.LastWriteTime : defaultFileTime;

    dirEntryList.push_front(DirectoryEntry::makeDotEntry(L".", fileTime));

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool CSDevice::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
    APP_ASSERT(argObjKey.meansDir());

    return mQueryObject->qoListObjects(CONT_CALLER argObjKey, pDirEntryList);
}

FILEIO_LENGTH_T CSDevice::getObjectAndWriteFile(CALLER_ARG const ObjectKey& argObjKey,
    const std::filesystem::path& argOutputPath, FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength)
{
    return mExecuteApi->GetObjectAndWriteFile(CONT_CALLER argObjKey, argOutputPath, argOffset, argLength);
}

bool CSDevice::putObject(CALLER_ARG const ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();

    if (!mExecuteApi->PutObject(CONT_CALLER argObjKey, argFileInfo, argSourcePath))
    {
        errorW(L"fault: PutObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    // �L���b�V���E����������폜

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool CSDevice::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    if (!mExecuteApi->DeleteObject(CONT_CALLER argObjKey))
    {
        errorW(L"fault: DeleteObject");
        return false;
    }

    // �L���b�V���E����������폜

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool CSDevice::deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys)
{
    NEW_LOG_BLOCK();

    if (!mExecuteApi->DeleteObjects(CONT_CALLER argBucket, argKeys))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    // �L���b�V���E����������폜

    for (const auto& key: argKeys)
    {
        const auto optObjKey{ ObjectKey::fromObjectPath(argBucket, key) };
        if (optObjKey)
        {
            const auto num = mQueryObject->qoDeleteCache(CONT_CALLER *optObjKey);
            traceW(L"cache delete num=%d, optObjKey=%s", num, optObjKey->c_str());
        }
        else
        {
            errorW(L"fault: fromObjectPath argBucket=%s key=%s", argBucket.c_str(), key.c_str());
        }
    }

    return true;
}


// EOF
