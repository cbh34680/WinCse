#include "CSDevice.hpp"

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

DirInfoType CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey)
{
    //THREAD_SAFE();
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // ディレクトリの存在確認

    // クラウドストレージではディレクトリの概念は存在しないので
    // 本来は外部から listObjects() を実行して、ロジックで判断するが
    // 意味的にわかりにくくなるので、ここで吸収する

    UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, argObjKey.str() };   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        DirInfoType dirInfo;

        if (argObjKey.meansDir())
        {
            return mQueryObject->unsafeHeadObject_CheckDir(CONT_CALLER argObjKey);
        }
        else
        {
            APP_ASSERT(argObjKey.meansFile());

            return mQueryObject->unsafeHeadObject(CONT_CALLER argObjKey);
        }

    }   // 名前のロックを解除 (safeShare の生存期間)
}

bool CSDevice::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, argObjKey.str() };   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        return mQueryObject->unsafeListObjects(CONT_CALLER argObjKey, pDirInfoList);

    }   // 名前のロックを解除 (safeShare の生存期間)
}

bool CSDevice::listDisplayObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
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
        const auto itParent = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
        {
            return wcscmp(dirInfo->FileNameBuf, L"..") == 0;
        });

        if (itParent == dirInfoList.cend())
        {
            dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir(L".."));
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
        return wcscmp(dirInfo->FileNameBuf, L".") == 0;
    });

    if (itCurr == dirInfoList.cend())
    {
        dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir(L"."));
    }
    else
    {
        const auto save{ *itCurr };
        dirInfoList.erase(itCurr);
        dirInfoList.insert(dirInfoList.cbegin(), save);
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

        UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, searchObjKey.str() };   // 名前への参照を登録
        {
            const auto safeShare{ unsafeShare.lock() }; // 名前のロック

            if (mRuntimeEnv->StrictFileTimestamp)
            {
                // ディレクトリにファイル名を付与して HeadObject を取得

                const auto mergeDirInfo{ mQueryObject->unsafeHeadObject(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;

                    //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                }
            }
            else
            {
                // ディレクトリにファイル名を付与して HeadObject のキャッシュを検索

                const auto mergeDirInfo{ mQueryObject->headObjectCacheOnly(CONT_CALLER searchObjKey) };
                if (mergeDirInfo)
                {
                    dirInfo->FileInfo = mergeDirInfo->FileInfo;
                }
            }

            if (mQueryObject->isNegative(CONT_CALLER searchObjKey))
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