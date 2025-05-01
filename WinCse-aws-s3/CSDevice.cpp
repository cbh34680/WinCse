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

    // バケット一覧の先読み

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

    // クラウドストレージではディレクトリの概念は存在しないので、空のディレクトリ・オブジェクトが存在しないときは
    // ListObjects() を実行して、ロジックで判断することになる。
    // それでは利用する側が意味的にわかりにくくなるので、ここで吸収する

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

    // CMD と同じ動きをさせるため ".", ".." が存在しない場合に追加する

    // "C:\WORK" のようにドライブ直下のディレクトリでは ".." が表示されない動作に合わせる

    if (argObjKey.isObject())
    {
        const auto itParent = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
        {
            return dirInfo->FileName == L"..";
        });

        if (itParent == dirInfoList.cend())
        {
            // 親ディレクトリなので、ディレクトリ・オブジェクトとして登録

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
        // 自ディレクトリなので、ディレクトリ・オブジェクトとして登録

        dirInfoList.insert(dirInfoList.cbegin(), this->makeDirInfoOfDir_1(L"."));
    }
    else
    {
        const auto save{ *itCurr };
        dirInfoList.erase(itCurr);
        dirInfoList.insert(dirInfoList.cbegin(), save);
    }

    //
    // dirInfoList の内容を HeadObject で取得したキャッシュとマージ
    //

    for (auto& dirInfo: dirInfoList)
    {
        if (dirInfo->FileName == L"." || dirInfo->FileName == L"..")
        {
            continue;
        }

        // ディレクトリにファイル名を付与

        const auto searchObjKey{ argObjKey.append(dirInfo->FileName) };

        APP_ASSERT(searchObjKey.isObject());

        DirInfoPtr mergeDirInfo;

        if (mRuntimeEnv->StrictFileTimestamp)
        {
            // HeadObject を取得

            this->headObject(CONT_CALLER searchObjKey, &mergeDirInfo);
        }
        else
        {
            // HeadObject のキャッシュを検索

            mQueryObject->qoHeadObjectFromCache(CONT_CALLER searchObjKey, &mergeDirInfo);
        }

        if (mergeDirInfo)
        {
            // 取得出来たら差し替え

            dirInfo = std::move(mergeDirInfo);
        }

        if (mQueryObject->qoIsInNegativeCache(CONT_CALLER searchObjKey))
        {
            // リージョン違いなどで HeadObject が失敗したものに HIDDEN 属性を追加

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

    // キャッシュ・メモリから削除
    //
    // 上記で作成したディレクトリがキャッシュに反映されていない状態で
    // 利用されてしまうことを回避するために事前に削除しておき、改めてキャッシュを作成させる

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    // headObject() は必須ではないが、作成直後に属性が参照されることに対応

    this->headObject(CONT_CALLER argObjKey, nullptr);

    return true;
}


// EOF
