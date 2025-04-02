#include "AwsS3.hpp"

using namespace WinCseLib;

// -----------------------------------------------------------------------------------
//
// 外部から呼び出されるインターフェース
//

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool AwsS3::headObject_File(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_File);
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // ファイルの存在確認

    // 直接的なキャッシュを優先して調べる
    // --> 更新されたときを考慮

    if (!this->unsafeHeadObjectWithCache(CONT_CALLER argObjKey, pFileInfo))
    {
        traceW(L"not found: unsafeHeadObjectWithCache, argObjKey=%s", argObjKey.c_str());
        return false;
    }

    return true;
}

bool AwsS3::headObject_Dir(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject_Dir);
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // ディレクトリの存在確認

    // クラウドストレージではディレクトリの概念は存在しないので
    // 本来は外部から listObjects() を実行して、ロジックで判断するが
    // 意味的にわかりにくくなるので、ここで吸収する

    // 直接的なキャッシュを優先して調べる
    // --> 更新されたときを考慮

    DirInfoListType dirInfoList;

    if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::CheckDirExists, &dirInfoList))
    {
        traceW(L"not found: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());
        return false;
    }

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
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    DirInfoListType dirInfoList;

    if (!this->unsafeListObjectsWithCache(CONT_CALLER argObjKey, Purpose::Display, &dirInfoList))
    {
        traceW(L"fault: unsafeListObjectsWithCache, argObjKey=%s", argObjKey.c_str());
        return false;
    }

    if (pDirInfoList)
    {
        for (auto& dirInfo: dirInfoList)
        {
            if (!FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
            {
                const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

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
            }
        }

        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

//
// 以降は override ではないもの
//

// レポートの生成
void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();
    APP_ASSERT(fp);

    this->unsafeReportObjectCache(CONT_CALLER fp);
}

// 古いキャッシュの削除
int AwsS3::deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    return this->unsafeDeleteOldObjectCache(CONT_CALLER threshold);
}

int AwsS3::clearObjectCache(CALLER_ARG0)
{
    THREAD_SAFE();

    return this->unsafeClearObjectCache(CONT_CALLER0);
}

int AwsS3::deleteObjectCache(CALLER_ARG const ObjectKey& argObjKey)
{
    THREAD_SAFE();
    APP_ASSERT(argObjKey.valid());

    return this->unsafeDeleteObjectCache(CONT_CALLER argObjKey);
}

// EOF