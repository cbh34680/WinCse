#include "AwsS3.hpp"

using namespace WinCseLib;

// -----------------------------------------------------------------------------------
//
// 外部から呼び出されるインターフェース
//

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
//static std::mutex gGuard;
//#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

struct ObjectListShare : public SharedBase { };
static ShareStore<ObjectListShare> gObjectListShare;


bool AwsS3::headObject_File(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_File);
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        // ファイルの存在確認

        // 直接的なキャッシュを優先して調べる
        // --> 更新されたときを考慮

        if (!this->unsafeHeadObjectWithCache(CONT_CALLER argObjKey, pFileInfo))
        {
            traceW(L"not found: unsafeHeadObjectWithCache, argObjKey=%s", argObjKey.c_str());
            return false;
        }

        return true;

    }   // 名前のロックを解除 (safeShare の生存期間)
}

bool AwsS3::headObject_Dir(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_Dir);
    //THREAD_SAFE();
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // ディレクトリの存在確認

    // クラウドストレージではディレクトリの概念は存在しないので
    // 本来は外部から listObjects() を実行して、ロジックで判断するが
    // 意味的にわかりにくくなるので、ここで吸収する

    // 直接的なキャッシュを優先して調べる
    // --> 更新されたときを考慮

    DirInfoListType dirInfoList;

    UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() }; // 名前のロック

        if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::CheckDirExists, &dirInfoList))
        {
            //traceW(L"fault: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());

            return false;
        }

    }   // 名前のロックを解除 (safeShare の生存期間)

    APP_ASSERT(dirInfoList.size() == 1);

    // ディレクトリの場合は FSP_FSCTL_FILE_INFO に適当な値を埋める
    // ... 取得した要素の情報([0]) がファイルの場合もあるので、編集が必要

    const auto dirInfo{ makeDirInfo_dir(argObjKey.key(), (*dirInfoList.begin())->FileInfo.LastWriteTime) };

    if (pFileInfo)
    {
        *pFileInfo = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList)
{
    StatsIncr(listObjects);
    //THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    DirInfoListType dirInfoList;

    {
        UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, argObjKey.str());   // 名前への参照を登録
        {
            const auto safeShare{ unsafeShare.lock() }; // 名前のロック

            if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::Display, &dirInfoList))
            {
                traceW(L"fault: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());
                return false;
            }

        }   // 名前のロックを解除 (safeShare の生存期間)
    }

    if (pDirInfoList)
    {
        // 表示用のリストは CMD と同じ動きをさせるため ".", ".." が存在しない場合に追加する
        //
        // "C:\WORK" のようにドライブ直下のディレクトリでは ".." が表示されない動作に合わせる

        if (argObjKey.hasKey())
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
        for (auto& dirInfo: dirInfoList)
        {
            if (FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
            {
                // ディレクトリは関係ない

                continue;
            }

            const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

            UnprotectedShare<ObjectListShare> unsafeShare(&gObjectListShare, fileObjKey.str());   // 名前への参照を登録
            {
                const auto safeShare{ unsafeShare.lock() }; // 名前のロック

                if (mConfig.strictFileTimestamp)
                {
                    // ディレクトリにファイル名を付与して HeadObject を取得

                    FSP_FSCTL_FILE_INFO mergeFileInfo;

                    if (this->unsafeHeadObjectWithCache(CONT_CALLER fileObjKey, &mergeFileInfo))
                    {
                        dirInfo->FileInfo = mergeFileInfo;

                        //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                    }
                }
                else
                {
                    // ディレクトリにファイル名を付与して HeadObject のキャッシュを検索

                    DirInfoType mergeDirInfo;

                    if (this->unsafeGetPositiveCache_File(CONT_CALLER fileObjKey, &mergeDirInfo))
                    {
                        // HeadObject の結果が取れたとき
                        //
                        // --> メタ情報に更新日時などが記録されているので、xcopy のように
                        //     ファイル属性を扱う操作が行えるはず
                        // 
                        // --> *dirInfo = *mergeDirInfo としたほうが効率的だが、一応 メモリコピー

                        dirInfo->FileInfo = mergeDirInfo->FileInfo;

                        //traceW(L"merge fileInfo fileObjKey=%s", fileObjKey.c_str());
                    }
                }

                if (this->unsafeIsInNegativeCache_File(CONT_CALLER fileObjKey))
                {
                    // リージョン違いなどで HeadObject が失敗したものに HIDDEN 属性を追加

                    dirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                }

            }   // 名前のロックを解除 (safeShare の生存期間)
        }

        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// EOF