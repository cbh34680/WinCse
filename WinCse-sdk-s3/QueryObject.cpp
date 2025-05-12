#include "QueryObject.hpp"

using namespace CSELIB;
using namespace CSESS3;


bool QueryObject::qoHeadObjectFromCache(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry) const
{
    return mCacheHeadObject.coGet(CONT_CALLER argObjKey, pDirEntry);
}

bool QueryObject::qoIsInNegativeCache(CALLER_ARG const ObjectKey& argObjKey) const
{
    return mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey); 
}

void QueryObject::qoReportCache(CALLER_ARG FILE* fp) const
{
    mCacheHeadObject.coReport(CONT_CALLER fp);
    mCacheListObjects.coReport(CONT_CALLER fp);
}

int QueryObject::qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    const auto delHead = mCacheHeadObject.coDeleteByTime(CONT_CALLER threshold);
    const auto delList = mCacheListObjects.coDeleteByTime(CONT_CALLER threshold);

    return delHead + delList;
}

int QueryObject::qoClearCache(CALLER_ARG0)
{
    const auto now{ std::chrono::system_clock::now() };

    return this->qoDeleteOldCache(CONT_CALLER now);
}

int QueryObject::qoDeleteCache(CALLER_ARG const ObjectKey& argObjKey)
{
    const auto delHead = mCacheHeadObject.coDeleteByKey(CONT_CALLER argObjKey);
    const auto delList = mCacheListObjects.coDeleteByKey(CONT_CALLER argObjKey);

    return delHead + delList;
}

bool QueryObject::qoHeadObject(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argObjKey.isBucket());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // ネガティブ・キャッシュを調べる

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirEntryType dirEntry;

    if (mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirEntry))
    {
        // ポジティブ・キャッシュ中に見つかった
    }
    else
    {
        // HeadObject API の実行

        if (!mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirEntry))
        {
            // ネガティブ・キャッシュに登録

            // 存在チェックも兼ねているので、ここは traceW() のまま
            traceW(L"not found: headObject argObjKey=%s", argObjKey.c_str());

            mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        // キャッシュにコピー

        traceW(L"coSet argObjKey=%s dirEntry=%s", argObjKey.c_str(), dirEntry->str().c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirEntry);
    }

    APP_ASSERT(dirEntry);

    if (pDirEntry)
    {
        *pDirEntry = std::move(dirEntry);
    }

    return true;
}

bool QueryObject::qoHeadObjectOrListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryType* pDirEntry)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(argObjKey.isObject());
    APP_ASSERT(argObjKey.meansDir());

    traceW(L"argObjKey=%s", argObjKey.c_str());

    // ネガティブ・キャッシュを調べる

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirEntryType dirEntry;

    if (!mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirEntry))
    {
        if (!mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirEntry))
        {
            // 下位の層にオブジェクトが存在するが、自層に空のディレクトリ・オブジェクト
            // は存在しない状況
            //
            // 例)
            //  "s3://bucket/dir/" が存在しない状態で、以下を実行
            //
            //      $ aws s3 cp file.txt s3://bucket/dir/subdir/

            traceW(L"fault: HeadObject");

            // 親ディレクトリの CommonPrefix からディレクトリ情報を取得

            std::wstring parentDir;
            std::wstring searchName;

            if (!SplitObjectKey(argObjKey.str(), &parentDir, &searchName))
            {
                errorW(L"fault: SplitObjectKey argObjKey=%s", argObjKey.c_str());
                return false;
            }

            APP_ASSERT(!parentDir.empty());
            APP_ASSERT(!searchName.empty());
            APP_ASSERT(searchName.back() == L'/');

            const auto optParentDir{ ObjectKey::fromObjectPath(parentDir) };
            if (!optParentDir)
            {
                traceW(L"fault: fromObjectPath parentDir=%s", parentDir.c_str());
                return false;
            }

            DirEntryListType dirEntryList;

            // 親ディレクトリのキャッシュから調べる
            // --> 通常はここにあるはず

            if (!mCacheListObjects.coGet(CONT_CALLER *optParentDir, &dirEntryList))
            {
                // 存在しない場合は親のディレクトリに対して ListObjectsV2() API を実行する

                if (!mExecuteApi->ListObjects(CONT_CALLER *optParentDir, &dirEntryList))
                {
                    // エラーの時はネガティブ・キャッシュに登録

                    errorW(L"fault: ListObjects optParentDir=%s", optParentDir->c_str());

                    mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                    return false;
                }
            }

            // 親ディレクトリのリストから名前の一致するものを探す

            const auto it = std::find_if(dirEntryList.cbegin(), dirEntryList.cend(), [&searchName](const auto& item)
            {
                return item->mName == searchName;
            });

            if (it == dirEntryList.cend())
            {
                // 見つからなかったらネガティブ・キャッシュに登録

                traceW(L"fault: not found in Parent CommonPrefix argObjKey=%s", argObjKey.c_str());

                mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                return false;
            }

            dirEntry = *it;
        }

        APP_ASSERT(dirEntry);

        // キャッシュにコピー

        traceW(L"coSet argObjKey=%s dirEntry=%s", argObjKey.c_str(), dirEntry->str().c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirEntry);
    }

    APP_ASSERT(dirEntry);

    if (pDirEntry)
    {
        *pDirEntry = std::move(dirEntry);
    }

    return true;
}

bool QueryObject::qoListObjects(CALLER_ARG const ObjectKey& argObjKey, DirEntryListType* pDirEntryList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());

    // ネガティブ・キャッシュを調べる

    if (mCacheListObjects.coIsNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirEntryListType dirEntryList;

    if (mCacheListObjects.coGet(CONT_CALLER argObjKey, &dirEntryList))
    {
        // ポジティブ・キャッシュに見つかった
    }
    else
    {
        // ポジティブ・キャッシュ中に見つからない

        if (!mExecuteApi->ListObjects(CONT_CALLER argObjKey, &dirEntryList))
        {
            // ネガティブ・キャッシュに登録

            errorW(L"fault: ListObjects argObjKey=%s", argObjKey.c_str());

            mCacheListObjects.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        // ポジティブ・キャッシュにコピー

        traceW(L"coSet argObjKey=%s dirEntryList.size=%zu", argObjKey.c_str(), dirEntryList.size());

        mCacheListObjects.coSet(CONT_CALLER argObjKey, dirEntryList);
    }

    if (pDirEntryList)
    {
        *pDirEntryList = std::move(dirEntryList);
    }

    return true;
}

// EOF