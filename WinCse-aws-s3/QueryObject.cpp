#include "QueryObject.hpp"

using namespace WCSE;


DirInfoType QueryObject::headObjectCacheOnly(CALLER_ARG const ObjectKey& argObjKey)
{
    DirInfoType dirInfo;

    if (mCacheHeadObject.get(CONT_CALLER argObjKey, &dirInfo))
    {
        return dirInfo;
    }

    return nullptr;
}

bool QueryObject::isNegative(CALLER_ARG const ObjectKey& argObjKey)
{
    return mCacheHeadObject.isNegative(CONT_CALLER argObjKey); 
}

void QueryObject::reportCache(CALLER_ARG FILE* fp)
{
    mCacheHeadObject.report(CONT_CALLER fp);
    mCacheListObjects.report(CONT_CALLER fp);
}

int QueryObject::deleteOldCache(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    const auto delHead = mCacheHeadObject.deleteByTime(CONT_CALLER threshold);
    const auto delList = mCacheListObjects.deleteByTime(CONT_CALLER threshold);

    return delHead + delList;
}

int QueryObject::clearCache(CALLER_ARG0)
{
    const auto now{ std::chrono::system_clock::now() };

    return this->deleteOldCache(CONT_CALLER now);
}

int QueryObject::deleteCache(CALLER_ARG const ObjectKey& argObjKey)
{
    const auto delHead = mCacheHeadObject.deleteByKey(CONT_CALLER argObjKey);
    const auto delList = mCacheListObjects.deleteByKey(CONT_CALLER argObjKey);

    return delHead + delList;
}

DirInfoType QueryObject::unsafeHeadObject(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(!argObjKey.isBucket());

    // ネガティブ・キャッシュを調べる

    if (mCacheHeadObject.isNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return nullptr;
    }

    // ポジティブ・キャッシュを調べる

    DirInfoType dirInfo;

    if (mCacheHeadObject.get(CONT_CALLER argObjKey, &dirInfo))
    {
        // ポジティブ・キャッシュ中に見つかった
    }
    else
    {
        NEW_LOG_BLOCK();

        // HeadObject API の実行

        if (mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirInfo))
        {
            traceW(L"success: HeadObject");
        }
        else
        {
            // ネガティブ・キャッシュに登録


            traceW(L"fault: headObject");

            mCacheHeadObject.addNegative(CONT_CALLER argObjKey);

            return nullptr;
        }

        // キャッシュにコピー

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheHeadObject.set(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    return dirInfo;
}

DirInfoType QueryObject::unsafeHeadObject_CheckDir(CALLER_ARG const ObjectKey& argObjKey)
{
    APP_ASSERT(!argObjKey.isBucket());

    // ネガティブ・キャッシュを調べる

    if (mCacheHeadObject.isNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return nullptr;
    }

    // ポジティブ・キャッシュを調べる

    DirInfoType dirInfo;

    if (mCacheHeadObject.get(CONT_CALLER argObjKey, &dirInfo))
    {
        // ポジティブ・キャッシュ中に見つかった
    }
    else
    {
        NEW_LOG_BLOCK();

        if (mExecuteApi->HeadObject(CONT_CALLER argObjKey, &dirInfo))
        {
            traceW(L"success: HeadObject");
        }
        else
        {
            traceW(L"fault: HeadObject");

            // 親ディレクトリの CommonPrefix からディレクトリ情報を取得

            std::wstring parentDir;
            std::wstring searchName;

            const auto b = SplitPath(argObjKey.str(), &parentDir, &searchName);
            APP_ASSERT(b);

            DirInfoListType dirInfoList;

            if (!mExecuteApi->ListObjectsV2(CONT_CALLER ObjectKey::fromPath(parentDir), true, 0, &dirInfoList))
            {
                // エラーの時はネガティブ・キャッシュに登録

                traceW(L"fault: ListObjectsV2");

                mCacheHeadObject.addNegative(CONT_CALLER argObjKey);

                return nullptr;
            }

            // 親ディレクトリのリストから名前の一致するものを探す

            for (const auto& it: dirInfoList)
            {
                std::wstring fileName{ it->FileNameBuf };

                if (FA_IS_DIRECTORY(it->FileInfo.FileAttributes))
                {
                    // FileNameBuf の内容は L"dirname" なので、L"dirname/" に直す

                    fileName += L'/';
                }

                if (fileName == searchName)
                {
                    dirInfo = makeDirInfo(it->FileNameBuf, it->FileInfo.LastWriteTime, FILE_ATTRIBUTE_DIRECTORY | mRuntimeEnv->DefaultFileAttributes);
                    break;
                }
            }

            if (!dirInfo)
            {
                // 見つからなかったらネガティブ・キャッシュに登録

                traceW(L"not found in Parent CommonPrefix");

                mCacheHeadObject.addNegative(CONT_CALLER argObjKey);

                return nullptr;
            }
        }

        // キャッシュにコピー

        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheHeadObject.set(CONT_CALLER argObjKey, dirInfo);
    }

    APP_ASSERT(dirInfo);

    return dirInfo;
}

bool QueryObject::unsafeListObjects(CALLER_ARG const ObjectKey& argObjKey,
    DirInfoListType* pDirInfoList /* nullable */)
{
    APP_ASSERT(argObjKey.meansDir());

    // ネガティブ・キャッシュを調べる

    if (mCacheListObjects.isNegative(CONT_CALLER argObjKey))
    {
        // ネガティブ・キャッシュ中に見つかった

        return false;
    }

    // ポジティブ・キャッシュを調べる

    DirInfoListType dirInfoList;

    if (mCacheListObjects.get(CONT_CALLER argObjKey, &dirInfoList))
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

            mCacheListObjects.addNegative(CONT_CALLER argObjKey);

            return false;
        }

        // ポジティブ・キャッシュにコピー

        NEW_LOG_BLOCK();
        traceW(L"add argObjKey=%s", argObjKey.c_str());

        mCacheListObjects.set(CONT_CALLER argObjKey, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// EOF