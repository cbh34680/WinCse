#include "AwsS3.hpp"
#include "ObjectCache.hpp"

using namespace WinCseLib;


static ObjectCache gObjectCache;

void AwsS3::unsafeReportObjectCache(CALLER_ARG FILE* fp)
{
    gObjectCache.report(CONT_CALLER fp);
}

int AwsS3::unsafeDeleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    return gObjectCache.deleteByTime(CONT_CALLER threshold);
}

int AwsS3::unsafeClearObjectCache(CALLER_ARG0)
{
    return gObjectCache.deleteByTime(CONT_CALLER std::chrono::system_clock::now());
}

int AwsS3::unsafeDeleteObjectCache(CALLER_ARG const WinCseLib::ObjectKey& argObjKey)
{
    return gObjectCache.deleteByKey(CONT_CALLER argObjKey);
}

bool AwsS3::unsafeHeadObjectWithCache(CALLER_ARG const ObjectKey& argObjKey, FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansFile());

    //traceW(L"argObjKey=%s", argObjKey.c_str());

    // ポジティブ・キャッシュを調べる

    DirInfoType dirInfo;
    const bool inPositiveCache = gObjectCache.getPositive_File(CONT_CALLER argObjKey, &dirInfo);

    if (inPositiveCache)
    {
        // ポジティブ・キャッシュ中に見つかった

        APP_ASSERT(dirInfo);
        //traceW(L"found in positive-cache");
    }
    else
    {
        //traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative_File(CONT_CALLER argObjKey))
        {
            // ネガティブ・キャッシュ中に見つかった

            //traceW(L"found in negative cache");

            return false;
        }

        // HeadObject API の実行

        //traceW(L"do HeadObject");

        dirInfo = this->apicallHeadObject(CONT_CALLER argObjKey);
        if (!dirInfo)
        {
            // ネガティブ・キャッシュに登録

            //traceW(L"add negative");

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

bool AwsS3::unsafeListObjectsWithCache(CALLER_ARG const ObjectKey& argObjKey,
    const Purpose argPurpose, DirInfoListType* pDirInfoList /* nullable */)
{
    //NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    //traceW(L"purpose=%s, argObjKey=%s", PurposeString(argPurpose), argObjKey.c_str());

    // ポジティブ・キャッシュを調べる

    DirInfoListType dirInfoList;
    const bool inPositiveCache = gObjectCache.getPositive(CONT_CALLER argObjKey, argPurpose, &dirInfoList);

    if (inPositiveCache)
    {
        // ポジティブ・キャッシュ中に見つかった

        //traceW(L"found in positive-cache");
    }
    else
    {
        //traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative(CONT_CALLER argObjKey, argPurpose))
        {
            // ネガティブ・キャッシュ中に見つかった

            //traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() の実行

        //traceW(L"call doListObjectV2");

        if (!this->apicallListObjectsV2(CONT_CALLER argPurpose, argObjKey, &dirInfoList))
        {
            // 実行時エラー、またはオブジェクトが見つからない

            //traceW(L"object not found");

            // ネガティブ・キャッシュに登録

            //traceW(L"add negative");
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

bool AwsS3::unsafeGetPositiveCache_File(CALLER_ARG const ObjectKey& argObjKey, DirInfoType* pDirInfo)
{
    APP_ASSERT(argObjKey.meansFile());

    return gObjectCache.getPositive_File(CONT_CALLER argObjKey, pDirInfo); 
}

// EOF