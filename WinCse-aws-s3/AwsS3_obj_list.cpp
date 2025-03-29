#include "AwsS3.hpp"
#include "ObjectCache.hpp"


using namespace WinCseLib;

// -----------------------------------------------------------------------------------
//
// キャッシュを含めた検索をするブロック
//
static ObjectCache gObjectCache;

bool AwsS3::unlockHeadObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    DirInfoType dirInfo;

    // ポジティブ・キャッシュを調べる

    if (gObjectCache.getPositive_File(CONT_CALLER argObjKey, &dirInfo))
    {
        APP_ASSERT(dirInfo);

        traceW(L"found in positive-cache");
    }

    if (!dirInfo)
    {
        traceW(L"not found in positive-cache");

        // ネガティブ・キャッシュを調べる

        if (gObjectCache.isInNegative_File(CONT_CALLER argObjKey))
        {
            // ネガティブ・キャッシュにある == データは存在しない

            traceW(L"found in negative cache");

            return false;
        }

        // HeadObject API の実行
        traceW(L"do HeadObject");

        dirInfo = this->apicallHeadObject(CONT_CALLER argObjKey);
        if (!dirInfo)
        {
            // ネガティブ・キャッシュに登録

            traceW(L"add negative");

            gObjectCache.addNegative_File(CONT_CALLER argObjKey);

            return false;
        }

        // キャッシュにコピー

        gObjectCache.setPositive_File(CONT_CALLER argObjKey, dirInfo);
    }

    if (pFileInfo)
    {
        (*pFileInfo) = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::unlockListObjects(CALLER_ARG const ObjectKey& argObjKey,
    const Purpose argPurpose, DirInfoListType* pDirInfoList /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"purpose=%s, argObjKey=%s", PurposeString(argPurpose), argObjKey.c_str());

    // ポジティブ・キャッシュを調べる

    DirInfoListType dirInfoList;
    const bool inCache = gObjectCache.getPositive(CONT_CALLER argObjKey, argPurpose, &dirInfoList);

    if (inCache)
    {
        // ポジティブ・キャッシュ中に見つかった

        traceW(L"found in positive-cache");
    }
    else
    {
        traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative(CONT_CALLER argObjKey, argPurpose))
        {
            // ネガティブ・キャッシュ中に見つかった

            traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() の実行
        traceW(L"call doListObjectV2");

        if (!this->apicallListObjectsV2(CONT_CALLER argPurpose, argObjKey, &dirInfoList))
        {
            // 実行時エラー、またはオブジェクトが見つからない

            traceW(L"object not found");

            // ネガティブ・キャッシュに登録

            traceW(L"add negative");
            gObjectCache.addNegative(CONT_CALLER argObjKey, argPurpose);

            return false;
        }

        // ポジティブ・キャッシュにコピー

        gObjectCache.setPositive(CONT_CALLER argObjKey, argPurpose, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

void AwsS3::unlockReportObjectCache(CALLER_ARG FILE* fp)
{
    gObjectCache.report(CONT_CALLER fp);
}

int AwsS3::unlockDeleteOldObjects(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    return gObjectCache.deleteOldRecords(CONT_CALLER threshold);
}

int AwsS3::unlockClearObjects(CALLER_ARG0)
{
    return gObjectCache.deleteOldRecords(CONT_CALLER std::chrono::system_clock::now());
}

int AwsS3::unlockDeleteCacheByObjectKey(CALLER_ARG const WinCseLib::ObjectKey& argObjKey)
{
    return gObjectCache.deleteByObjectKey(CONT_CALLER argObjKey);
}

// -----------------------------------------------------------------------------------
//
// 外部IF に Purpose を記述させないためのブロック
// (意味が分かりにくくなるので)
//

bool AwsS3::unlockListObjects_Display(CALLER_ARG
    const WinCseLib::ObjectKey& argObjKey, DirInfoListType* pDirInfoList /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

#if 0
    if (!this->unlockListObjects(CONT_CALLER argObjKey, Purpose::Display, pDirInfoList))
    {
        traceW(L"fault: unlockListObjects");
        return false;
    }

#else
    DirInfoListType dirInfoList;

    if (!this->unlockListObjects(CONT_CALLER argObjKey, Purpose::Display, &dirInfoList))
    {
        traceW(L"fault: unlockListObjects");
        return false;
    }

    for (auto& dirInfo: dirInfoList)
    {
        if (!FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
        {
            // ディレクトリにファイル名を付与して HeadObject のキャッシュを検索

            const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

            DirInfoType mergeDirInfo;

            if (gObjectCache.getPositive_File(CONT_CALLER fileObjKey, &mergeDirInfo))
            {
                // HeadObject の結果が取れたとき
                //
                // --> メタ情報に更新日時などが記録されているので、xcopy のように
                //     ファイル属性を扱う操作が行えるはず
                // 
                // --> *dirInfo = *mergeDirInfo としたほうが効率的だが、一応 メモリコピー

                dirInfo->FileInfo = mergeDirInfo->FileInfo;
            }
        }
    }

    *pDirInfoList = std::move(dirInfoList);

#endif
    return true;
}

bool AwsS3::unlockHeadObject_File(CALLER_ARG
    const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // 直接的なキャッシュを優先して調べる
    // --> 更新されたときを考慮

    if (this->unlockHeadObject(CONT_CALLER argObjKey, pFileInfo))
    {
        traceW(L"unlockHeadObject: found");

        return true;
    }

    traceW(L"unlockHeadObject: not found");

    return false;
}

DirInfoType AwsS3::unlockListObjects_Dir(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // 直接的なキャッシュを優先して調べる
    // --> 更新されたときを考慮

    DirInfoListType dirInfoList;

    if (this->unlockListObjects(CONT_CALLER argObjKey, Purpose::CheckDirExists, &dirInfoList))
    {
        APP_ASSERT(dirInfoList.size() == 1);

        traceW(L"unlockListObjects: found");

        // ディレクトリの場合は FSP_FSCTL_FILE_INFO に適当な値を埋める
        // ... 取得した要素の情報([0]) がファイルの場合もあるので、編集が必要

        return makeDirInfo_dir(argObjKey.key(), (*dirInfoList.begin())->FileInfo.LastWriteTime);
    }

    traceW(L"unlockListObjects: not found");

    return nullptr;
}

// -----------------------------------------------------------------------------------
//
// 外部から呼び出されるインターフェース
//

//
// ここから下のメソッドは THREAD_SAFE マクロによる修飾が必要
//
static std::mutex gGuard;
#define THREAD_SAFE() std::lock_guard<std::mutex> lock_(gGuard)

bool AwsS3::headObject(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject);
    THREAD_SAFE();
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.hasKey());

    traceW(L"ObjectKey=%s", argObjKey.c_str());

    // キーの最後の文字に "/" があるかどうかでファイル/ディレクトリを判断
    //
    if (argObjKey.meansDir())
    {
        // ディレクトリの存在確認

        // クラウドストレージではディレクトリの概念は存在しないので
        // 本来は外部から listObjects() を実行して、ロジックで判断するが
        // 意味的にわかりにくくなるので、ここで吸収する

        const auto dirInfo{ this->unlockListObjects_Dir(CONT_CALLER argObjKey) };
        if (!dirInfo)
        {
            traceW(L"fault: unlockListObjects");
            return false;
        }

        if (pFileInfo)
        {
            *pFileInfo = dirInfo->FileInfo;
        }
    }
    else
    {
        // ファイルの存在確認

        if (!this->unlockHeadObject_File(CONT_CALLER argObjKey, pFileInfo))
        {
            traceW(L"fault: unlockHeadObject");
            return false;
        }
    }

    return true;
}

bool AwsS3::listObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoListType* pDirInfoList /* nullable */)
{
    StatsIncr(listObjects);
    THREAD_SAFE();
    APP_ASSERT(argObjKey.meansDir());

    return this->unlockListObjects_Display(CONT_CALLER argObjKey, pDirInfoList);
}

//
// 以降は override ではないもの
//

// レポートの生成
void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();
    APP_ASSERT(fp);

    this->unlockReportObjectCache(CONT_CALLER fp);
}

// 古いキャッシュの削除
int AwsS3::deleteOldObjects(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    return this->unlockDeleteOldObjects(CONT_CALLER threshold);
}

int AwsS3::clearObjects(CALLER_ARG0)
{
    THREAD_SAFE();

    return this->unlockClearObjects(CONT_CALLER0);
}

int AwsS3::deleteCacheByObjectKey(CALLER_ARG const ObjectKey& argObjKey)
{
    THREAD_SAFE();
    APP_ASSERT(argObjKey.valid());

    return this->unlockDeleteCacheByObjectKey(CONT_CALLER argObjKey);
}

// EOF