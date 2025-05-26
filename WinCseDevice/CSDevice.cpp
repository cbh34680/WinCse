#include "CSDevice.hpp"
#include <urlmon.h>

using namespace CSELIB;

namespace CSEDVC {

CSELIB::FILEIO_LENGTH_T writeStreamFromFile(CALLER_ARG const std::ostream* argOutputStream,
    const std::filesystem::path& argInputPath, CSELIB::FILEIO_OFFSET_T argInputOffset, CSELIB::FILEIO_LENGTH_T argInputLength)
{
    NEW_LOG_BLOCK();

    CSELIB::FileHandle file = ::CreateFileW(
        argInputPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: CreateFileW lerr=%lu", lerr);
        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argInputOffset;

    if (::SetFilePointerEx(file.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: SetFilePointerEx lerr=%lu file=%s", lerr, file.str().c_str());

        return -1LL;
    }

    static thread_local char buffer[CSELIB::FILEIO_BUFFER_SIZE];
    const CSELIB::FILEIO_LENGTH_T bufferSize = _countof(buffer);

    auto* pbuf = argOutputStream->rdbuf();
    auto remainingTotal = argInputLength;

    while (remainingTotal > 0)
    {
        if (!argOutputStream->good())
        {
            errorW(L"fault: no good");
            return -1LL;
        }

        DWORD bytesRead;
        if (!::ReadFile(file.handle(), buffer, static_cast<DWORD>(min(bufferSize, remainingTotal)), &bytesRead, NULL))
        {
            const auto lerr = ::GetLastError();

            errorW(L"fault: ReadFile lerr=%lu", lerr);
            return -1LL;
        }

        traceW(L"bytesRead=%lu", bytesRead);

        auto remainingWrite = static_cast<std::streamsize>(bytesRead);
        auto* pos = buffer;

        while (remainingWrite > 0)
        {
            traceW(L"remainingWrite=%lld", remainingWrite);

            const auto bytesWritten = pbuf->sputn(pos, remainingWrite);
            if (bytesWritten <= 0)
            {
                errorW(L"fault: sputn");
                return -1LL;
            }

            pos += bytesWritten;
            remainingWrite -= bytesWritten;

            traceW(L"bytesWritten=%lld remainingWrite=%lld", bytesWritten, remainingWrite);
        }

        remainingTotal -= bytesRead;

        traceW(L"remainingTotal=%lld", remainingTotal);
    }

    APP_ASSERT(remainingTotal == 0);

    return argInputLength;
}

CSELIB::FILEIO_LENGTH_T writeFileFromStream(CALLER_ARG
    const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOutputOffset,
    const std::istream* argInputStream, CSELIB::FILEIO_LENGTH_T argInputLength)
{
    NEW_LOG_BLOCK();

    // ファイルを開き argOffset の位置にポインタを移動

    CSELIB::FileHandle file = ::CreateFileW
    (
        argOutputPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: CreateFileW lerr=%lu argOutputPath=%s argOffset=%lld", lerr, argOutputPath.c_str(), argOutputOffset);

        return -1LL;
    }

    LARGE_INTEGER li{};
    li.QuadPart = argOutputOffset;

    if (::SetFilePointerEx(file.handle(), li, NULL, FILE_BEGIN) == 0)
    {
        const auto lerr = ::GetLastError();
        errorW(L"fault: SetFilePointerEx lerr=%lu file=%s", lerr, file.str().c_str());

        return -1LL;
    }

    // 取得した内容をファイルに出力

    auto* pbuf = argInputStream->rdbuf();
    auto remainingTotal = argInputLength;

    static thread_local char buffer[CSELIB::FILEIO_BUFFER_SIZE];
    const CSELIB::FILEIO_LENGTH_T bufferSize = _countof(buffer);

    while (remainingTotal > 0)
    {
        // バッファにデータを読み込む

        if (!argInputStream->good())
        {
            errorW(L"fault: no good");
            return -1LL;
        }

        const auto bytesRead = pbuf->sgetn(buffer, min(remainingTotal, bufferSize));
        if (bytesRead <= 0)
        {
            errorW(L"fault: sgetn");
            return -1LL;
        }

        traceW(L"bytesRead=%lld", bytesRead);

        // ファイルにデータを書き込む

        auto* pos = buffer;
        auto remainingWrite = bytesRead;

        while (remainingWrite > 0)
        {
            traceW(L"remainingWrite=%lld", remainingWrite);

            DWORD bytesWritten;
            if (!::WriteFile(file.handle(), pos, static_cast<DWORD>(remainingWrite), &bytesWritten, NULL))
            {
                const auto lerr = ::GetLastError();
                errorW(L"fault: WriteFile lerr=%lu", lerr);

                return -1LL;
            }

            pos += bytesWritten;
            remainingWrite -= bytesWritten;

            traceW(L"bytesWritten=%lu remainingWrite=%lld", bytesWritten, remainingWrite);
        }

        remainingTotal -= bytesRead;

        traceW(L"remainingTotal=%lld", remainingTotal);
    }

    return argInputLength;
}

std::wstring getContentType(CALLER_ARG UINT64 argFileSize, PCWSTR argInputPath, const std::wstring& argKey)
{
    NEW_LOG_BLOCK();

    if (argFileSize == 0)
    {
        // ファイルが空の時は拡張子で判定する
    }
    else
    {
        FileHandle file = ::CreateFileW(
            argInputPath,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (file.valid())
        {
            BYTE bytes[256];
            DWORD bytesRead;

            if (::ReadFile(file.handle(), bytes, sizeof(bytes), &bytesRead, NULL))
            {
                LPWSTR mimeType = nullptr;

                HRESULT hr = ::FindMimeFromData(nullptr, nullptr, bytes, bytesRead, nullptr, 0, &mimeType, 0);
                if (SUCCEEDED(hr))
                {
                    std::wstring ret{ mimeType };
                    ::CoTaskMemFree(mimeType);

                    if (ret == L"application/octet-stream")
                    {
                        // 内容から判定できないときは拡張子で判定する
                    }
                    else
                    {
                        return ret;
                    }
                }
                else
                {
                    traceW(L"fault: FindMimeFromData");
                }
            }
            else
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: ReadFile lerr=%lu", lerr);
            }

            file.close();
        }
        else
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: CreateFileW lerr=%lu", lerr);
        }
    }

    return GetMimeTypeFromFileName(argKey);
}

CSDevice::~CSDevice()
{
    this->OnSvcStop();
}

struct ListBucketsTask : public IOnDemandTask
{
    CSDevice* mThat;

    ListBucketsTask(CSDevice* argThat)
        :
        mThat(argThat)
    {
    }

    void run(int) override
    {
        NEW_LOG_BLOCK();

        //traceW(L"call ListBuckets");

        mThat->listBuckets(START_CALLER nullptr);
    }
};

bool CSDevice::headBucket(CALLER_ARG const std::wstring& argBucketName, CSELIB::DirEntryType* pDirEntry)
{
    return mQueryBucket->qbHeadBucket(CONT_CALLER argBucketName, pDirEntry);
}

bool CSDevice::listBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList)
{
    return mQueryBucket->qbListBuckets(CONT_CALLER pDirEntryList);
}

bool CSDevice::headObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry)
{
    APP_ASSERT(argObjKey.isObject());

    // クラウドストレージではディレクトリの概念は存在しないので、空のディレクトリ・オブジェクトが存在しないときは
    // ListObjects() を実行して、ロジックで判断することになる。
    // それでは利用する側が意味的にわかりにくくなるので、ここで吸収する

    if (argObjKey.meansDir())
    {
        return mQueryObject->qoHeadObjectOrListObjects(CONT_CALLER argObjKey, pDirEntry);
    }
    else
    {
        APP_ASSERT(argObjKey.meansFile());

        return mQueryObject->qoHeadObject(CONT_CALLER argObjKey, pDirEntry);
    }
}

bool CSDevice::headObjectOrCache_(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry)
{
    // listDisplayObjects の中でのみ利用される関数

    if (mRuntimeEnv->StrictFileTimestamp)
    {
        // HeadObject を取得

        return this->headObject(CONT_CALLER argObjKey, pDirEntry);
    }
    else
    {
        // HeadObject のキャッシュを検索

        return mQueryObject->qoHeadObjectFromCache(CONT_CALLER argObjKey, pDirEntry);
    }
}

bool CSDevice::listDisplayObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.meansDir());
    APP_ASSERT(pDirEntryList);

    DirEntryListType dirEntryList;

    if (!this->listObjects(CONT_CALLER argObjKey, &dirEntryList))
    {
        errorW(L"fault: listObjects");

        return false;
    }

    // dirEntryList の内容を HeadObject で取得したキャッシュとマージ

    for (auto& dirEntry: dirEntryList)
    {
        APP_ASSERT(dirEntry->mName != L"." && dirEntry->mName != L"..");

        // ディレクトリにファイル名を付与

        const auto searchObjKey{ argObjKey.append(dirEntry->mName) };
        APP_ASSERT(searchObjKey.isObject());

        DirEntryType mergeDirEntry;
        if (this->headObjectOrCache_(CONT_CALLER searchObjKey, &mergeDirEntry))
        {
            // キャッシュから取得出来たら差し替え

            traceW(L"merge searchObjKey=%s mergeDirEntry=%s", searchObjKey.c_str(), mergeDirEntry->str().c_str());
            dirEntry = std::move(mergeDirEntry);
        }

        if (mQueryObject->qoIsInNegativeCache(CONT_CALLER searchObjKey))
        {
            // リージョン違いなどで HeadObject が失敗したものに HIDDEN 属性を追加

            traceW(L"set hidden searchObjKey=%s", searchObjKey.c_str());
            dirEntry->mFileInfo.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
        }
    }

    // ドットエントリの追加 (CMD 対応)

    const auto it = std::min_element(dirEntryList.cbegin(), dirEntryList.cend(), [](const auto& l, const auto& r)
    {
        return l->mFileInfo.LastWriteTime < r->mFileInfo.LastWriteTime;
    });

    const FILETIME_100NS_T defaultFileTime = it == dirEntryList.cend()
        ? mRuntimeEnv->DefaultCommonPrefixTime : (*it)->mFileInfo.LastWriteTime;

    if (!argObjKey.isBucket())
    {
        // CMD の動作に合わせる
        //      C:\             ... ".", ".." は表示されない
        //      C:\dir          ... "." は表示される
        //      C:\dir\subdir   ... ".", ".." が表示される

        DirEntryType dirEntry;

        const auto optParentDir{ argObjKey.toParentDir() };
        if (optParentDir)
        {
            if (optParentDir->isBucket())
            {
                this->headBucket(CONT_CALLER optParentDir->bucket(), &dirEntry);
            }
            else
            {
                this->headObjectOrCache_(CONT_CALLER *optParentDir, &dirEntry);
            }
        }

        const FILETIME_100NS_T fileTime = dirEntry ? dirEntry->mFileInfo.LastWriteTime : defaultFileTime;

        dirEntryList.push_front(DirectoryEntry::makeDotEntry(L"..", fileTime));
    }

    DirEntryType dirEntry;

    if (argObjKey.isBucket())
    {
        this->headBucket(CONT_CALLER argObjKey.bucket(), &dirEntry);
    }
    else
    {
        this->headObjectOrCache_(CONT_CALLER argObjKey, &dirEntry);
    }

    const FILETIME_100NS_T fileTime = dirEntry ? dirEntry->mFileInfo.LastWriteTime : defaultFileTime;

    dirEntryList.push_front(DirectoryEntry::makeDotEntry(L".", fileTime));

    *pDirEntryList = std::move(dirEntryList);

    return true;
}

bool CSDevice::listObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList)
{
    APP_ASSERT(argObjKey.meansDir());

    return mQueryObject->qoListObjects(CONT_CALLER argObjKey, pDirEntryList);
}

FILEIO_LENGTH_T CSDevice::getObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey,
    const std::filesystem::path& argOutputPath, FILEIO_OFFSET_T argOffset, FILEIO_LENGTH_T argLength)
{
    return mApiClient->GetObjectAndWriteFile(CONT_CALLER argObjKey, argOutputPath, argOffset, argLength);
}

bool CSDevice::putObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->PutObject(CONT_CALLER argObjKey, argFileInfo, argInputPath))
    {
        errorW(L"fault: PutObject argObjKey=%s", argObjKey.c_str());
        return false;
    }

    // キャッシュ・メモリから削除

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool CSDevice::copyObject(CALLER_ARG const CSELIB::ObjectKey& argSrcObjKey, const CSELIB::ObjectKey& argDstObjKey)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->CopyObject(CONT_CALLER argSrcObjKey, argDstObjKey))
    {
        errorW(L"fault: CopyObject argSrcObjKey=%s argDstObjKey=%s", argSrcObjKey.c_str(), argDstObjKey.c_str());
        return false;
    }

    // キャッシュ・メモリから削除

    const auto numSrc = mQueryObject->qoDeleteCache(CONT_CALLER argSrcObjKey);
    traceW(L"cache delete numSrc=%d, argSrcObjKey=%s", numSrc, argSrcObjKey.c_str());


    const auto numDst = mQueryObject->qoDeleteCache(CONT_CALLER argDstObjKey);
    traceW(L"cache delete numDst=%d, argDstObjKey=%s", numDst, argDstObjKey.c_str());

    return true;
}

bool CSDevice::deleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->DeleteObject(CONT_CALLER argObjKey))
    {
        errorW(L"fault: DeleteObject");
        return false;
    }

    // キャッシュ・メモリから削除

    const auto num = mQueryObject->qoDeleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool CSDevice::deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys)
{
    NEW_LOG_BLOCK();

    if (!mApiClient->DeleteObjects(CONT_CALLER argBucket, argKeys))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    // キャッシュ・メモリから削除

    for (const auto& key: argKeys)
    {
        const auto optObjKey{ ObjectKey::fromObjectPath(argBucket, key) };
        if (optObjKey)
        {
            const auto num = mQueryObject->qoDeleteCache(CONT_CALLER *optObjKey);
            traceW(L"cache delete num=%d, optObjKey=%s", num, optObjKey->c_str());
        }
        else
        {
            errorW(L"fault: fromObjectPath argBucket=%s key=%s", argBucket.c_str(), key.c_str());
        }
    }

    return true;
}

}   // namespace CSEDVC

// EOF
