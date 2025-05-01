#include "CSDevice.hpp"

using namespace CSELIB;
using namespace CSEDAS3;


CSELIB::ICSDevice* NewCSDevice(PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[])
{
    std::map<std::wstring, IWorker*> workers;

    if (NamedWorkersToMap(argWorkers, &workers) <= 0)
    {
        return nullptr;
    }

    for (const auto key: { L"delayed", L"timer", })
    {
        if (workers.find(key) == workers.cend())
        {
            return nullptr;
        }
    }

    return new CSDevice(argIniSection, workers);
}

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
        traceW(L"fault: AwsS3A::OnSvcStart");
        return ntstatus;
    }

    // �o�P�b�g�ꗗ�̐�ǂ�

    getWorker(L"delayed")->addTask(new ListBucketsTask{ this });

    return STATUS_SUCCESS;
}

bool CSDevice::headBucket(CALLER_ARG const std::wstring& argBucketName, CSELIB::DirInfoPtr* pDirInfo)
{
    return mQueryBucket->qbHeadBucket(CONT_CALLER argBucketName, pDirInfo);
}

bool CSDevice::listBuckets(CALLER_ARG CSELIB::DirInfoPtrList* pDirInfoList)
{
    return mQueryBucket->qbListBuckets(CONT_CALLER pDirInfoList, {});
}

bool CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo)
{
    APP_ASSERT(argObjKey.isObject());

    // �N���E�h�X�g���[�W�ł̓f�B���N�g���̊T�O�͑��݂��Ȃ��̂ŁA��̃f�B���N�g���E�I�u�W�F�N�g�����݂��Ȃ��Ƃ���
    // ListObjects() �����s���āA���W�b�N�Ŕ��f���邱�ƂɂȂ�B
    // ����ł͗��p���鑤���Ӗ��I�ɂ킩��ɂ����Ȃ�̂ŁA�����ŋz������

    if (argObjKey.meansDir())
    {
        return mQueryObject->qoHeadObjectOrListObjects(CONT_CALLER argObjKey, pDirInfo);
    }
    else
    {
        APP_ASSERT(argObjKey.meansFile());

        return mQueryObject->qoHeadObject(CONT_CALLER argObjKey, pDirInfo);
    }
}

bool CSDevice::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtrList* pDirInfoList)
{
    APP_ASSERT(argObjKey.meansDir());
    APP_ASSERT(pDirInfoList);

    DirInfoPtrList dirInfoList;

    if (!this->listObjects(CONT_CALLER argObjKey, &dirInfoList))
    {
        NEW_LOG_BLOCK();
        traceW(L"fault: listObjects");

        return false;
    }

    // CMD �Ɠ��������������邽�� ".", ".." �����݂��Ȃ��ꍇ�ɒǉ�����

    // "C:\WORK" �̂悤�Ƀh���C�u�����̃f�B���N�g���ł� ".." ���\������Ȃ�����ɍ��킹��

    if (argObjKey.isObject())
    {
        const auto itParent = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
        {
            return dirInfo->FileName == L"..";
        });

        if (itParent == dirInfoList.cend())
        {
            // �e�f�B���N�g���Ȃ̂ŁA�f�B���N�g���E�I�u�W�F�N�g�Ƃ��ēo�^

            dirInfoList.insert(dirInfoList.cbegin(), this->makeDirInfoOfDir_1(L".."));
        }
        else
        {
            const auto save{ *itParent };
            dirInfoList.erase(itParent);
            dirInfoList.insert(dirInfoList.cbegin(), save);
        }
    }

    const auto itCurr = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
    {
        return dirInfo->FileName == L".";
    });

    if (itCurr == dirInfoList.cend())
    {
        // ���f�B���N�g���Ȃ̂ŁA�f�B���N�g���E�I�u�W�F�N�g�Ƃ��ēo�^

        dirInfoList.insert(dirInfoList.cbegin(), this->makeDirInfoOfDir_1(L"."));
    }
    else
    {
        const auto save{ *itCurr };
        dirInfoList.erase(itCurr);
        dirInfoList.insert(dirInfoList.cbegin(), save);
    }

    //
    // dirInfoList �̓��e�� HeadObject �Ŏ擾�����L���b�V���ƃ}�[�W
    //

    for (auto& dirInfo: dirInfoList)
    {
        if (dirInfo->FileName == L"." || dirInfo->FileName == L"..")
        {
            continue;
        }

        // �f�B���N�g���Ƀt�@�C������t�^

        const auto searchObjKey{ argObjKey.append(dirInfo->FileName) };

        APP_ASSERT(searchObjKey.isObject());

        DirInfoPtr mergeDirInfo;

        if (mRuntimeEnv->StrictFileTimestamp)
        {
            // HeadObject ���擾

            this->headObject(CONT_CALLER searchObjKey, &mergeDirInfo);
        }
        else
        {
            // HeadObject �̃L���b�V��������

            mQueryObject->qoHeadObjectFromCache(CONT_CALLER searchObjKey, &mergeDirInfo);
        }

        if (mergeDirInfo)
        {
            // �擾�o�����獷���ւ�

            dirInfo = std::move(mergeDirInfo);
        }

        if (mQueryObject->qoIsInNegativeCache(CONT_CALLER searchObjKey))
        {
            // ���[�W�����Ⴂ�Ȃǂ� HeadObject �����s�������̂� HIDDEN ������ǉ�

            dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
        }
    }

    *pDirInfoList = std::move(dirInfoList);

    return true;
}

bool CSDevice::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtrList* pDirInfoList)
{
    APP_ASSERT(argObjKey.meansDir());

    return mQueryObject->qoListObjects(CONT_CALLER argObjKey, pDirInfoList);
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
        traceW(L"fault: PutObject");
        return false;
    }

    // �L���b�V���E����������폜
    //
    // ��L�ō쐬�����f�B���N�g�����L���b�V���ɔ��f����Ă��Ȃ���Ԃ�
    // ���p����Ă��܂����Ƃ�������邽�߂Ɏ��O�ɍ폜���Ă����A���߂ăL���b�V�����쐬������

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    // headObject() �͕K�{�ł͂Ȃ����A�쐬����ɑ������Q�Ƃ���邱�ƂɑΉ�

    this->headObject(CONT_CALLER argObjKey, nullptr);

    return true;
}


// EOF
