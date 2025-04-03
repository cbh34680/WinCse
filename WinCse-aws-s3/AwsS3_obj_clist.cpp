#include "AwsS3.hpp"
#include "ObjectCache.hpp"

using namespace WinCseLib;


static ObjectCache gObjectCache;

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

    // ネガティブ・キャッシュを調べる

    if (gObjectCache.isInNegative(CONT_CALLER argObjKey, argPurpose))
    {
        // ネガティブ・キャッシュ中に見つかった

        //traceW(L"found in negative-cache");

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirInfoListType dirInfoList;
    const bool inPositiveCache = gObjectCache.getPositive(CONT_CALLER argObjKey, argPurpose, &dirInfoList);

    if (inPositiveCache)
    {
        // ポジティブ・キャッシュに見つかった

        //traceW(L"found in positive-cache");
    }
    else
    {
        // ポジティブ・キャッシュ中に見つからない

        //traceW(L"not found in positive-cache");

        // ListObjectV2() の実行

        //traceW(L"call doListObjectV2");

        bool delimiter = false;
        int limit = 0;

        switch (argPurpose)
        {
            case Purpose::CheckDirExists:
            {
                // ディレクトリの存在確認の為にだけ呼ばれるはず

                APP_ASSERT(!argObjKey.isBucket());

                limit = 1;

                break;
            }
            case Purpose::Display:
            {
                // DoReadDirectory() からのみ呼び出されるはず

                delimiter = true;

                break;
            }
            default:
            {
                APP_ASSERT(0);
            }
        }

        if (!this->apicallListObjectsV2(CONT_CALLER argObjKey, delimiter, limit, &dirInfoList))
        {
            // 実行時エラー、またはオブジェクトが見つからない

            //traceW(L"fault: apicallListObjectsV2");

            // ネガティブ・キャッシュに登録

            gObjectCache.addNegative(CONT_CALLER argObjKey, argPurpose);

            return false;
        }

        switch (argPurpose)
        {
            case Purpose::CheckDirExists:
            {
                // ディレクトリの存在確認の為にだけ呼ばれるはず

                if (dirInfoList.empty())
                {
                    // ネガティブ・キャッシュに登録

                    gObjectCache.addNegative(CONT_CALLER argObjKey, argPurpose);

                    return false;
                }

                APP_ASSERT(dirInfoList.size() == 1);

                break;
            }
            case Purpose::Display:
            {
                break;
            }
            default:
            {
                APP_ASSERT(0);
            }
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

bool AwsS3::unsafeIsInNegativeCache_File(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(argObjKey.meansFile());

    return gObjectCache.isInNegative_File(CONT_CALLER argObjKey); 
}


//
// 以降は公開されているが、gObjectCache がスレッド・セーフのため排他制御の必要ないもの
//

void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    gObjectCache.report(CONT_CALLER fp);
}

int AwsS3::deleteOldObjectCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    return gObjectCache.deleteByTime(CONT_CALLER threshold);
}

int AwsS3::clearObjectCache(CALLER_ARG0)
{
    return gObjectCache.deleteByTime(CONT_CALLER std::chrono::system_clock::now());
}

int AwsS3::deleteObjectCache(CALLER_ARG const WinCseLib::ObjectKey& argObjKey)
{
    return gObjectCache.deleteByKey(CONT_CALLER argObjKey);
}

// EOF