#include "AwsS3.hpp"

using namespace WCSE;

// -----------------------------------------------------------------------------------
//
// 外部から呼び出されるインターフェース
//

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
//static std::mutex gGuard;
//#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }

struct ObjectListShare : public SharedBase { };
static ShareStore<ObjectListShare> gObjectListShare;

bool AwsS3::headObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    //THREAD_SAFE();
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // ディレクトリの存在確認

    // クラウドストレージではディレクトリの概念は存在しないので
    // 本来は外部から listObjects() を実行して、ロジックで判断するが
    // 意味的にわかりにくくなるので、ここで吸収する

    DirInfoListType dirInfoList;

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        DirInfoType dirInfo;

        if (argObjKey.meansDir())
        {
            dirInfo = this->unsafeHeadObjectWithCache_CheckDir(CONT_CALLER argObjKey);
        }
        else
        {
            APP_ASSERT(argObjKey.meansFile());

            dirInfo = this->unsafeHeadObjectWithCache(CONT_CALLER argObjKey);
        }

        if (!dirInfo)
        {
            return false;
        }

        if (pFileInfo)
        {
            *pFileInfo = dirInfo->FileInfo;
        }

    }   // 名前のロックを解除 (safeShare の生存期間)

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    StatsIncr(listObjects);
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        return this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, pDirInfoList);

    }   // 名前のロックを解除 (safeShare の生存期間)
}

bool AwsS3::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());
    APP_ASSERT(pDirInfoList);

    DirInfoListType dirInfoList;

    if (!this->listObjects(CONT_CALLER argObjKey, &dirInfoList))
    {
        traceW(L"fault: listObjects");

        return false;
    }

    // CMD と同じ動きをさせるため ".", ".." が存在しない場合に追加する

    // "C:\WORK" のようにドライブ直下のディレクトリでは ".." が表示されない動作に合わせる

    if (argObjKey.isObject())
    {
        const auto itParent = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
        {
            return wcscmp(dirInfo->FileNameBuf, L"..") == 0;
        });

        if (itParent == dirInfoList.end())
        {
            dirInfoList.insert(dirInfoList.begin(), makeDirInfo_dir(L"..", mWorkDirCTime));
        }
        else
        {
            const auto save{ *itParent };
            dirInfoList.erase(itParent);
            dirInfoList.insert(dirInfoList.begin(), save);
        }
    }

    const auto itCurr = std::find_if(dirInfoList.begin(), dirInfoList.end(), [](const auto& dirInfo)
    {
        return wcscmp(dirInfo->FileNameBuf, L".") == 0;
    });

    if (itCurr == dirInfoList.end())
    {
        dirInfoList.insert(dirInfoList.begin(), makeDirInfo_dir(L".", mWorkDirCTime));
    }
    else
    {
        const auto save{ *itCurr };
        dirInfoList.erase(itCurr);
        dirInfoList.insert(dirInfoList.begin(), save);
    }

    //
    // HeadObject で取得したキャッシュとマージ
    //

    for (auto& dirInfo: dirInfoList)
    {
        if (wcscmp(dirInfo->FileNameBuf, L".") == 0 || wcscmp(dirInfo->FileNameBuf, L"..") == 0)
        {
            continue;
        }

        ObjectKey searchObjKey;

        if (FA_IS_DIRECTORY(dirInfo->FileInfo.FileAttributes))
        {
            searchObjKey = argObjKey.append(dirInfo->FileNameBuf).toDir();
        }
        else
        {
            searchObjKey = argObjKey.append(dirInfo->FileNameBuf);
        }

        APP_ASSERT(searchObjKey.isObject());

        UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, searchObjKey.str());   // 名前への参照を登録
        {
            const auto safeShare{ unsafeShare.lock() }; // 名前のロック

            if (mConfig.strictFileTimestamp)
            {
                // ディレクトリにファイル名を付与して HeadObject を取得

                const auto mergeDirInfo{ this->unsafeHeadObjectWithCache(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;

                    //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                }
            }
            else
            {
                // ディレクトリにファイル名を付与して HeadObject のキャッシュを検索

                const auto mergeDirInfo{ this->getCachedHeadObject(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;
                }
            }

            if (this->isNegativeHeadObject(CONT_CALLER searchObjKey))
            {
                // リージョン違いなどで HeadObject が失敗したものに HIDDEN 属性を追加

                dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }

        }   // 名前のロックを解除 (safeShare の生存期間)
    }

    *pDirInfoList = std::move(dirInfoList);

    return true;
}

// EOF