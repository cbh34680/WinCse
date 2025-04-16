#include "CSDevice.hpp"

using namespace WCSE;

// -----------------------------------------------------------------------------------
//
// 外部から呼び出されるインターフェース
//

//
// ここから下のメソッドは UnprotectedShare による排他制御が必要
//
//static std::mutex gGuard;
//#define THREAD_SAFE() std::lock_guard<std::mutex> lock_{ gGuard }

// 排他制御の必要な範囲を限定するため、グローバル変数にしている

struct ObjectListShare : public SharedBase { };
static ShareStore<ObjectListShare> gObjectListShare;

bool CSDevice::headObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo)
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

        if (argObjKey.meansDir())
        {
            return mQueryObject->unsafeHeadObject_CheckDir(CONT_CALLER argObjKey, pDirInfo);
        }
        else
        {
            APP_ASSERT(argObjKey.meansFile());

            return mQueryObject->unsafeHeadObject(CONT_CALLER argObjKey, pDirInfo);
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
            dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir1(L".."));
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
        dirInfoList.insert(dirInfoList.cbegin(), makeDirInfoDir1(L"."));
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

            DirInfoType mergeDirInfo;

            if (mRuntimeEnv->StrictFileTimestamp)
            {
                // ディレクトリにファイル名を付与して HeadObject を取得

                mQueryObject->unsafeHeadObject(CONT_CALLER searchObjKey, &mergeDirInfo);
            }
            else
            {
                // ディレクトリにファイル名を付与して HeadObject のキャッシュを検索

                mQueryObject->headObjectFromCache(CONT_CALLER searchObjKey, &mergeDirInfo);
            }

            if (mergeDirInfo)
            {
                dirInfo->FileInfo = mergeDirInfo->FileInfo;
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

// イレギュラーな対応
// 
// 元々は CSDevice_obj_rw.cpp に記述されていたが、XCOPY /V を実行したときに
// クローズ処理中に listObjectsV2 が呼ばれ、キャッシュ更新前のファイルサイズが利用されることで
// 検証に失敗してしまう。
// これに対応するため、PutObject とキャッシュの削除を listObjectsV2 のロックと同じ範囲で行う。

bool CSDevice::putObjectWithListLock(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    // 通常は操作対象とロックのキーが一致するが、ここでは親のディレクトリをロックしながら
    // ディレクトリ内のファイルを操作している。

    const auto parentDir{ argObjKey.toParentDir() };
    APP_ASSERT(parentDir);

    UnprotectedShare<ObjectListShare> unsafeShare{ &gObjectListShare, parentDir->str() };   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        if (!mExecuteApi->PutObject(CONT_CALLER argObjKey, argFileInfo, argSourcePath))
        {
            traceW(L"fault: PutObject");
            return false;
        }

        // キャッシュ・メモリから削除
        //
        // 上記で作成したディレクトリがキャッシュに反映されていない状態で
        // 利用されてしまうことを回避するために事前に削除しておき、改めてキャッシュを作成させる

        const auto num = mQueryObject->deleteCache(CONT_CALLER argObjKey);
        traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());
    }

    // headObject() は必須ではないが、作成直後に属性が参照されることに対応

    if (!this->headObject(CONT_CALLER argObjKey, nullptr))
    {
        traceW(L"fault: headObject");
        return false;
    }

    return true;
}

// EOF