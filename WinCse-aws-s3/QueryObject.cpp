#include "QueryObject.hpp"

using namespace CSELIB;
using namespace CSEDAS3;


bool QueryObject::qoHeadObjectFromCache(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) const noexcept
{
    return mCacheHeadObject.coGet(CONT_CALLER argObjKey, pDirInfo);
}

bool QueryObject::qoIsInNegativeCache(CALLER_ARG const ObjectKey& argObjKey) const noexcept
{
    return mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey); 
}

void QueryObject::qoReportCache(CALLER_ARG FILE* fp) const noexcept
{
    mCacheHeadObject.coReport(CONT_CALLER fp);
    mCacheListObjects.coReport(CONT_CALLER fp);
}

int QueryObject::qoDeleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold) noexcept
{
    const auto delHead = mCacheHeadObject.coDeleteByTime(CONT_CALLER threshold);
    const auto delList = mCacheListObjects.coDeleteByTime(CONT_CALLER threshold);

    return delHead + delList;
}

int QueryObject::qoClearCache(CALLER_ARG0) noexcept
{
    const auto now{ std::chrono::system_clock::now() };

    return this->qoDeleteOldCache(CONT_CALLER now);
}

int QueryObject::qoDeleteCache(CALLER_ARG const ObjectKey& argObjKey) noexcept
{
    const auto delHead = mCacheHeadObject.coDeleteByKey(CONT_CALLER argObjKey);
    const auto delList = mCacheListObjects.coDeleteByKey(CONT_CALLER argObjKey);

    return delHead + delList;
}

bool QueryObject::qoHeadObject(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) noexcept
{
    APP_ASSERT(!argObjKey.isBucket());

    // ネガティブ・キャッシュを調べる

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirInfoPtr dirInfo;

    if (mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirInfo))
    {
        // ポジティブ・キャッシュ中に見つかった
    }
    else
    {
        NEW_LOG_BLOCK();

        // HeadObject API の実行

        if (!mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirInfo))
        {
            // ネガティブ・キャッシュに登録

            traceW(L"fault: headObject");

            mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        traceW(L"success: HeadObject");

        // キャッシュにコピー

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    if (pDirInfo)
    {
        *pDirInfo = std::move(dirInfo);
    }

    return true;
}

bool QueryObject::qoHeadObjectOrListObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtr* pDirInfo) noexcept
{
    APP_ASSERT(argObjKey.isObject());
    APP_ASSERT(argObjKey.meansDir());

    // ネガティブ・キャッシュを調べる

    if (mCacheHeadObject.coIsNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirInfoPtr dirInfo;

    if (mCacheHeadObject.coGet(CONT_CALLER argObjKey, &dirInfo))
    {
        // ポジティブ・キャッシュ中に見つかった
    }
    else
    {
        NEW_LOG_BLOCK();

        if (mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirInfo))
        {
            // 空のディレクトリ・オブジェクト(ex. "dir/") が存在する状況

            traceW(L"success: HeadObject");
        }
        else
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

            const auto b = SplitObjectKey(argObjKey.str(), &parentDir, &searchName);
            APP_ASSERT(b);

            APP_ASSERT(!parentDir.empty());
            APP_ASSERT(!searchName.empty());
            APP_ASSERT(searchName.back() == L'/');

            const auto optParentDir{ ObjectKey::fromPath(parentDir) };

            if (!optParentDir)      // コンパイラの警告抑止
            {
                APP_ASSERT(0);

                return false;
            }

            DirInfoPtrList dirInfoList;

            // 親ディレクトリのキャッシュから調べる
            // --> 通常はここにあるはず

            if (!mCacheListObjects.coGet(CONT_CALLER *optParentDir, &dirInfoList))
            {
                // 存在しない場合は親のディレクトリに対して ListObjectsV2() API を実行する

                if (!mExecuteApi->ListObjectsV2(CONT_CALLER *optParentDir, true, 0, &dirInfoList))
                {
                    // エラーの時はネガティブ・キャッシュに登録

                    traceW(L"fault: ListObjectsV2");

                    mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                    return false;
                }
            }

            // 親ディレクトリのリストから名前の一致するものを探す

            for (const auto& it: dirInfoList)
            {
                if (it->FileName == searchName)
                {
                    // TODO: 引数の FileAttributes をチェック

                    dirInfo = makeDirInfoOfDir(it->FileName, it->FileInfo.LastWriteTime, it->FileInfo.FileAttributes | mRuntimeEnv->DefaultFileAttributes);
                    break;
                }
            }

            if (!dirInfo)
            {
                // 見つからなかったらネガティブ・キャッシュに登録

                traceW(L"not found in Parent CommonPrefix");

                mCacheHeadObject.coAddNegative(CONT_CALLER argObjKey);

                return false;
            }
        }

        APP_ASSERT(dirInfo);

        // キャッシュにコピー

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheHeadObject.coSet(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    if (pDirInfo)
    {
        *pDirInfo = std::move(dirInfo);
    }

    return true;
}

bool QueryObject::qoListObjects(CALLER_ARG const ObjectKey& argObjKey, DirInfoPtrList* pDirInfoList) noexcept
{
    APP_ASSERT(argObjKey.meansDir());

    // ネガティブ・キャッシュを調べる

    if (mCacheListObjects.coIsNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirInfoPtrList dirInfoList;

    if (mCacheListObjects.coGet(CONT_CALLER argObjKey, &dirInfoList))
    {
        // ポジティブ・キャッシュに見つかった
    }
    else
    {
        // ポジティブ・キャッシュ中に見つからない

        if (!mExecuteApi->ListObjectsV2(CONT_CALLER argObjKey, true, 0, &dirInfoList))
        {
            // ネガティブ・キャッシュに登録

            NEW_LOG_BLOCK();
            traceW(L"fault: ListObjectsV2");

            mCacheListObjects.coAddNegative(CONT_CALLER argObjKey);

            return false;
        }

        // ポジティブ・キャッシュにコピー

        NEW_LOG_BLOCK();
        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheListObjects.coSet(CONT_CALLER argObjKey, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// EOF