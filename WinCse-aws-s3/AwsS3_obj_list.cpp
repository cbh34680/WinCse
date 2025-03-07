#include "AwsS3.hpp"
#include "ObjectCache.hpp"


using namespace WinCseLib;


// -----------------------------------------------------------------------------------
//
// キャッシュを含めた検索をするブロック
//
extern ObjectCache gObjectCache;

bool AwsS3::unsafeHeadObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    bool alsoSearchCache, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKey.empty());
    APP_ASSERT(argKey.back() != L'/');

    traceW(L"bucket: %s, key: %s", argBucket.c_str(), argKey.c_str());

    DirInfoType dirInfo;

    if (alsoSearchCache)
    {
        // ポジティブ・キャッシュを調べる

        if (gObjectCache.getPositive_File(CONT_CALLER argBucket, argKey, &dirInfo))
        {
            APP_ASSERT(dirInfo);

            traceW(L"found in positive-cache");
        }
    }

    if (!dirInfo)
    {
        if (alsoSearchCache)
        {
            traceW(L"not found in positive-cache");

            // ネガティブ・キャッシュを調べる

            if (gObjectCache.isInNegative_File(CONT_CALLER argBucket, argKey))
            {
                // ネガティブ・キャッシュにある == データは存在しない

                traceW(L"found in negative cache");

                return false;
            }
        }

        // HeadObject API の実行
        traceW(L"do HeadObject");

        dirInfo = this->apicallHeadObject(CONT_CALLER argBucket, argKey);
        if (!dirInfo)
        {
            // ネガティブ・キャッシュに登録

            traceW(L"add negative");

            gObjectCache.addNegative_File(CONT_CALLER argBucket, argKey);

            return false;
        }

        // キャッシュにコピー

        gObjectCache.setPositive_File(CONT_CALLER argBucket, argKey, dirInfo);
    }

    if (pFileInfo)
    {
        (*pFileInfo) = dirInfo->FileInfo;
    }

    return true;
}

bool AwsS3::unsafeListObjects(CALLER_ARG const Purpose argPurpose,
    const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(argBucket.back() != L'/');

    if (!argKey.empty())
    {
        APP_ASSERT(argKey.back() == L'/');
    }

    traceW(L"purpose=%s, bucket=%s, key=%s",
        PurposeString(argPurpose), argBucket.c_str(), argKey.c_str());

    // ポジティブ・キャッシュを調べる

    DirInfoListType dirInfoList;
    const bool inCache = gObjectCache.getPositive(CONT_CALLER argPurpose, argBucket, argKey, &dirInfoList);

    if (inCache)
    {
        // ポジティブ・キャッシュ中に見つかった

        traceW(L"found in positive-cache");
    }
    else
    {
        traceW(L"not found in positive-cache");

        if (gObjectCache.isInNegative(CONT_CALLER argPurpose, argBucket, argKey))
        {
            // ネガティブ・キャッシュ中に見つかった

            traceW(L"found in negative-cache");

            return false;
        }

        // ListObjectV2() の実行
        traceW(L"call doListObjectV2");

        if (!this->apicallListObjectsV2(CONT_CALLER argPurpose, argBucket, argKey, &dirInfoList))
        {
            // 実行時エラー、またはオブジェクトが見つからない

            traceW(L"object not found");

            // ネガティブ・キャッシュに登録

            traceW(L"add negative");
            gObjectCache.addNegative(CONT_CALLER argPurpose, argBucket, argKey);

            return false;
        }

        // ポジティブ・キャッシュにコピー

        gObjectCache.setPositive(CONT_CALLER argPurpose, argBucket, argKey, dirInfoList);
    }

    if (pDirInfoList)
    {
        *pDirInfoList = std::move(dirInfoList);
    }

    return true;
}

// -----------------------------------------------------------------------------------
//
// 外部IF から呼び出されるブロック
//

bool AwsS3::unsafeListObjects_Display(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    StatsIncr(_unsafeListObjects_Display);

    return this->unsafeListObjects(CONT_CALLER Purpose::Display, argBucket, argKey, pDirInfoList);
}

//
// 表示用のキャッシュ (Purpose::Display) の中から、引数に合致する
// ファイルの情報を取得する
//
DirInfoType AwsS3::findFileInParentDirectry(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    StatsIncr(_findFileInParentDirectry);

    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());
    APP_ASSERT(!argKey.empty());

    traceW(L"bucket=[%s] key=[%s]", argBucket.c_str(), argKey.c_str());

    // キーから親ディレクトリを取得

    auto tokens{ SplitW(argKey, L'/', false) };
    APP_ASSERT(!tokens.empty());

    // ""                      ABORT
    // "dir"                   OK
    // "dir/"                  OK
    // "dir/key.txt"           OK
    // "dir/key.txt/"          OK
    // "dir/subdir/key.txt"    OK
    // "dir/subdir/key.txt/"   OK

    auto filename{ tokens.back() };
    APP_ASSERT(!filename.empty());
    tokens.pop_back();

    // 検索対象の親ディレクトリ

    auto parentDir{ JoinW(tokens, L'/', false) };
    if (parentDir.empty())
    {
        // バケットのルート・ディレクトリから検索

        // "" --> ""
    }
    else
    {
        // サブディレクトリから検索

        // "dir"        --> "dir/"
        // "dir/subdir" --> "dir/subdir/"

        parentDir += L'/';
    }

    // 検索対象のファイル名 (ディレクトリ名)

    if (argKey.back() == L'/')
    {
        // SplitW() で "/" が除かれてしまうので、argKey に "dir/" や "dir/file.txt/"
        // が指定されているときは filename に "/" を付与

        filename += L'/';
    }

    traceW(L"parentDir=[%s] filename=[%s]", parentDir.c_str(), filename.c_str());

    // Purpose::Display として保存されたキャッシュを取得

    DirInfoListType dirInfoList;
    const bool inCache = gObjectCache.getPositive(CONT_CALLER Purpose::Display, argBucket, parentDir, &dirInfoList);

    if (!inCache)
    {
        // 子孫のオブジェクトを探すときには、親ディレクトリはキャッシュに存在するはず
        // なので、基本的には通過しないはず

        traceW(L"not found in positive-cache, check it", argBucket.c_str(), parentDir.c_str());
        return nullptr;
    }

    const auto it = std::find_if(dirInfoList.begin(), dirInfoList.end(), [&filename](const auto& dirInfo)
    {
        std::wstring name{ dirInfo->FileNameBuf };

        if (name == L"." || name == L"..")
        {
            return false;
        }

        if (dirInfo->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // FSP_FSCTL_DIR_INFO の FileNameBuf にはディレクトリであっても
            // "/" で終端していないので、比較のために "/" を付与する

            name += L'/';
        }

        return filename == name;
    });

    if (it == dirInfoList.end())
    {
        // DoGetSecurityByName はディレクトリから存在チェックを始めるので
        // ファイル名に対して "dir/file.txt/" のような検索を始める
        // ここを通過するのは、その場合のみだと思う

        traceW(L"not found in parent-dir", argBucket.c_str(), filename.c_str());
        return nullptr;
    }

    return *it;
}

bool AwsS3::unsafeHeadObject_File(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    StatsIncr(_unsafeHeadObject_File);

    NEW_LOG_BLOCK();

    traceW(L"bucket=%s key=%s", argBucket.c_str(), argKey.c_str());

    // 直接的なキャッシュを優先して調べる
    // --> 更新されたときを考慮

    if (this->unsafeHeadObject(CONT_CALLER argBucket, argKey, true, pFileInfo))
    {
        traceW(L"unsafeHeadObject: found");

        return true;
    }

    traceW(L"unsafeHeadObject: not found");

    // 親ディレクトリから調べる

    const auto dirInfo{ findFileInParentDirectry(CONT_CALLER argBucket, argKey) };
    if (dirInfo)
    {
        traceW(L"findFileInParentDirectry: found");

        if (pFileInfo)
        {
            *pFileInfo = dirInfo->FileInfo;
        }

        return true;
    }

    traceW(L"findFileInParentDirectry: not found");

    return false;
}

DirInfoType AwsS3::unsafeListObjects_Dir(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey)
{
    StatsIncr(_unsafeListObjects_Dir);

    NEW_LOG_BLOCK();

    traceW(L"bucket=%s key=%s", argBucket.c_str(), argKey.c_str());

    // 直接的なキャッシュを優先して調べる
    // --> 更新されたときを考慮

    DirInfoListType dirInfoList;

    if (this->unsafeListObjects(CONT_CALLER Purpose::CheckDir, argBucket, argKey, &dirInfoList))
    {
        APP_ASSERT(dirInfoList.size() == 1);

        traceW(L"unsafeListObjects: found");

        // ディレクトリの場合は FSP_FSCTL_FILE_INFO に適当な値を埋める
        // ... 取得した要素の情報([0]) がファイルの場合もあるので、編集が必要

        return mallocDirInfoW_dir(argKey, argBucket, (*dirInfoList.begin())->FileInfo.ChangeTime);
    }

    traceW(L"unsafeListObjects: not found");

    // 親ディレクトリから調べる

    return findFileInParentDirectry(CONT_CALLER argBucket, argKey);
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
ObjectCache gObjectCache;


bool AwsS3::headObject(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    StatsIncr(headObject);

    THREAD_SAFE();

    NEW_LOG_BLOCK();
    APP_ASSERT(!argBucket.empty());

    bool ret = false;

    traceW(L"bucket: %s, key: %s", argBucket.c_str(), argKey.c_str());

    // キーの最後の文字に "/" があるかどうかでファイル/ディレクトリを判断
    //
    if (argKey.empty() || (!argKey.empty() && argKey.back() == L'/'))
    {
        // ディレクトリの存在確認

        const auto dirInfo{ this->unsafeListObjects_Dir(CONT_CALLER argBucket, argKey) };
        if (dirInfo)
        {
            if (pFileInfo)
            {
                *pFileInfo = dirInfo->FileInfo;
            }

            ret = true;
        }
        else
        {
            traceW(L"fault: unsafeListObjects");
        }
    }
    else
    {
        // ファイルの存在確認

        if (this->unsafeHeadObject_File(CONT_CALLER argBucket, argKey, pFileInfo))
        {
            ret = true;
        }
        else
        {
            traceW(L"fault: unsafeHeadObject");
            return false;
        }
    }

    return ret;
}

bool AwsS3::headObject_File_SkipCacheSearch(CALLER_ARG
    const std::wstring& argBucket, const std::wstring& argKey,
    FSP_FSCTL_FILE_INFO* pFileInfo /* nullable */)
{
    // shouldDownload() 内で使用しており、対象はファイルのみ
    // キャッシュは探さず api を実行する

    THREAD_SAFE();

    return this->unsafeHeadObject(CONT_CALLER argBucket, argKey, false, pFileInfo);
}

bool AwsS3::listObjects(CALLER_ARG const std::wstring& argBucket, const std::wstring& argKey,
    DirInfoListType* pDirInfoList)
{
    StatsIncr(listObjects);

    THREAD_SAFE();

    return this->unsafeListObjects_Display(CONT_CALLER argBucket, argKey, pDirInfoList);
}

// レポートの生成
void AwsS3::reportObjectCache(CALLER_ARG FILE* fp)
{
    THREAD_SAFE();

    gObjectCache.report(CONT_CALLER fp);
}

// 古いキャッシュの削除
void AwsS3::deleteOldObjects(CALLER_ARG std::chrono::system_clock::time_point threshold)
{
    THREAD_SAFE();

    gObjectCache.deleteOldRecords(CONT_CALLER threshold);
}

// EOF