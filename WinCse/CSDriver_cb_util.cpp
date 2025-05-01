#include "CSDriver.hpp"

using namespace CSELIB;


static FILEIO_LENGTH_T getFileSize(const std::filesystem::path& argPath)
{
    WIN32_FILE_ATTRIBUTE_DATA cacheFileInfo{};

    if (!::GetFileAttributesExW(argPath.c_str(), GetFileExInfoStandard, &cacheFileInfo))
    {
        return -1LL;
    }

    LARGE_INTEGER li{};
    li.HighPart = cacheFileInfo.nFileSizeHigh;
    li.LowPart = cacheFileInfo.nFileSizeLow;

    return li.QuadPart;
}

static bool syncFileTimes(HANDLE hFile, const FSP_FSCTL_FILE_INFO& fileInfo)
{
    NEW_LOG_BLOCK();

    FILETIME ftCreation;
    FILETIME ftLastAccess;
    FILETIME ftLastWrite;

    WinFileTime100nsToWinFile(fileInfo.CreationTime, &ftCreation);
    ::GetSystemTimeAsFileTime(&ftLastAccess);
    WinFileTime100nsToWinFile(fileInfo.LastWriteTime, &ftLastWrite);

    if (!::SetFileTime(hFile, &ftCreation, &ftLastAccess, &ftLastWrite))
    {
        traceW(L"fault: SetFileTime");
        return false;
    }

    traceW(L"ftCreation=%s",  WinFileTimeToLocalTimeStringW(ftCreation).c_str());
    traceW(L"ftLastAccess=%s", WinFileTimeToLocalTimeStringW(ftLastAccess).c_str());
    traceW(L"ftLastWrite=%s", WinFileTimeToLocalTimeStringW(ftLastWrite).c_str());

    return true;
}

class FilePart
{
    EventHandle mDone;
    CSELIB::FILEIO_LENGTH_T mResult = -1LL;

public:
    const CSELIB::FILEIO_OFFSET_T mOffset;
    const CSELIB::FILEIO_LENGTH_T mLength;

    std::atomic<bool> mInterrupt = false;

    explicit FilePart(CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) noexcept
        :
        mOffset(argOffset),
        mLength(argLength)
    {
        mDone = ::CreateEventW(NULL,
            TRUE,				// 手動リセットイベント
            FALSE,				// 初期状態：非シグナル状態
            NULL);

        APP_ASSERT(mDone.valid());
    }

    HANDLE getEvent() noexcept
    {
        return mDone.handle();
    }

    void setResult(CSELIB::FILEIO_LENGTH_T argResult) noexcept
    {
        mResult = argResult;
        const auto b = ::SetEvent(mDone.handle());					// シグナル状態に設定
        APP_ASSERT(b);
    }

    CSELIB::FILEIO_LENGTH_T getResult() const noexcept
    {
        return mResult;
    }

    bool isError() const noexcept
    {
        return mResult < 0;
    }

    ~FilePart()
    {
        mDone.close();
    }
};

struct ReadPartTask : public IOnDemandTask
{
    ICSDevice* mDevice;
    const ObjectKey mObjKey;
    const std::filesystem::path mOutputPath;
    std::shared_ptr<FilePart> mFilePart;

    ReadPartTask(
        ICSDevice* argDevice,
        const ObjectKey& argObjKey,
        const std::filesystem::path& argOutputPath,
        std::shared_ptr<FilePart> argFilePart)
        :
        mDevice(argDevice),
        mObjKey(argObjKey),
        mOutputPath(argOutputPath),
        mFilePart(argFilePart)
    {
    }

    void run(int argThreadIndex) override
    {
        NEW_LOG_BLOCK();

        CSELIB::FILEIO_LENGTH_T readBytes = -1LL;

        try
        {
            if (mFilePart->mInterrupt)
            {
                traceW(L"@%d Interruption request received", argThreadIndex);
            }
            else
            {
                traceW(L"@%d getObjectAndWriteFile", argThreadIndex);

                readBytes = mDevice->getObjectAndWriteFile(START_CALLER mObjKey, mOutputPath, mFilePart->mOffset, mFilePart->mLength);
            }
        }
        catch (const std::exception& ex)
        {
            traceA("catch exception: what=[%s]", ex.what());
        }
        catch (...)
        {
            traceW(L"catch unknown");
        }

        // 結果を設定し、シグナル状態に変更
        // --> WaitForSingleObject で待機しているスレッドのロックが解除される

        mFilePart->setResult(readBytes);
    }

    void cancelled(CALLER_ARG0) noexcept
    {
        NEW_LOG_BLOCK();

        traceW(L"set Interrupt");

        mFilePart->mInterrupt = true;
    }
};

namespace CSEDRV
{

bool makeCacheFilePath(const std::filesystem::path& argDir, const std::wstring& argName, std::filesystem::path* pPath)
{
    if (!std::filesystem::is_directory(argDir))
    {
        return false;
    }

    std::wstring nameSha256;

    const auto ntstatus = ComputeSHA256W(argName, &nameSha256);
    if (!NT_SUCCESS(ntstatus))
    {
        return false;
    }

    // 先頭の 2Byte はディレクトリ名

    auto filePath{ argDir / nameSha256.substr(0, 2) };

    std::error_code ec;
    std::filesystem::create_directory(filePath, ec);

    if (ec)
    {
        return false;
    }

    filePath.append(nameSha256.substr(2));

    *pPath = std::move(filePath);

    return true;
}

NTSTATUS updateFileInfo(HANDLE hFile, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    // ファイル・ハンドルの情報を取得

    FSP_FSCTL_FILE_INFO fileInfo;

    const auto ntstatus = GetFileInfoInternal(hFile, &fileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: WriteFile");
        return ntstatus;
    }

    if (fileInfo.FileSize < pFileInfo->FileSize)
    {
        // ダウンロードされていない部分もあるので、サイズはリモートのものを採用

        fileInfo.FileSize       = pFileInfo->FileSize;
        fileInfo.AllocationSize = pFileInfo->AllocationSize;
    }

    *pFileInfo = fileInfo;

    return STATUS_SUCCESS;
}

NTSTATUS syncAttributes(const std::filesystem::path& cacheFilePath, const DirInfoPtr& remoteDirInfo)
{
    NEW_LOG_BLOCK();

    FileHandle file = ::CreateFileW(
        cacheFilePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (file.invalid())
    {
        traceW(L"fault: CreateFileW");
        return FspNtStatusFromWin32(::GetLastError());
    }

    FSP_FSCTL_FILE_INFO localInfo;
    const auto ntstatus = GetFileInfoInternal(file.handle(), &localInfo);

    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return ntstatus;
    }

    if (localInfo.CreationTime  == remoteDirInfo->FileInfo.CreationTime &&
        localInfo.LastWriteTime == remoteDirInfo->FileInfo.LastWriteTime)
    {
        // 二つのタイムスタンプが同じときは同期中と考える

        traceW(L"In sync");
    }
    else
    {
        // タイムスタンプが異なるときは、ダウンロードを促すためにファイルを切り詰める

        if (localInfo.FileSize > 0)
        {
            // ファイルポインタを移動
#if 0
            if (::SetFilePointer(file.handle(), 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
            {
                traceW(L"fault: SetFilePointer");
                return FspNtStatusFromWin32(::GetLastError());
            }
#endif

            // ファイルを切り詰める

            if (!::SetEndOfFile(file.handle()))
            {
                traceW(L"fault: SetEndOfFile");
                return FspNtStatusFromWin32(::GetLastError());
            }
        }

        // タイムスタンプを同期

        if (!syncFileTimes(file.handle(), remoteDirInfo->FileInfo))
        {
            traceW(L"fault: syncFileTime");
            return FspNtStatusFromWin32(::GetLastError());
        }
    }

    return STATUS_SUCCESS;

}   // syncAttributes

NTSTATUS syncContent(CSDriver* that, FileContext* ctx, FILEIO_OFFSET_T argReadOffset, FILEIO_LENGTH_T argReadLength)
{
    NEW_LOG_BLOCK();

    if (ctx->mFileInfoRef->FileSize == 0)
    {
        traceW(L"Empty content");
        return STATUS_SUCCESS;
    }

    // ファイル・ハンドルからローカル・キャッシュのファイル名を取得

    std::filesystem::path cacheFilePath;

    if (!GetFileNameFromHandle(ctx->getHandle(), &cacheFilePath))
    {
        traceW(L"fault: GetFileNameFromHandle");
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"cacheFilePath=%s", cacheFilePath.c_str());

    // キャッシュ・ファイルのサイズを取得

    const auto cacheFileSize = getFileSize(cacheFilePath);
    if (cacheFileSize < 0)
    {
        traceW(L"fault: getFileSize");
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"cacheFileSize=%lld", cacheFileSize);

    // リモートの属性情報とファイル・サイズを比較

    if (cacheFileSize >= (FILEIO_LENGTH_T)ctx->mFileInfoRef->FileSize)
    {
        // 全てダウンロード済なので OK

        traceW(L"All content has been downloaded");
        return STATUS_SUCCESS;
    }

    // Read する範囲とファイル・サイズを比較

    const FILEIO_LENGTH_T fileSizeToRead = argReadOffset + argReadLength;
    if (cacheFileSize >= fileSizeToRead)
    {
        // Read 範囲のデータは存在するので OK

        traceW(L"Download not required");
        return STATUS_SUCCESS;
    }

    traceW(L"fileSizeToRead=%lld", fileSizeToRead);

    // Read 範囲のデータが不足しているのでダウンロードを実施

    //const auto BYTE_PART_SIZE = CSELIB::FILESIZE_1MiBll * that->mRuntimeEnv->TransferPerSizeMib;
    const auto BYTE_PART_SIZE = CSELIB::FILESIZE_1Bll   * 10;

    traceW(L"BYTE_PART_SIZE=%lld", BYTE_PART_SIZE);

    // 必要となるファイル・サイズから既に存在するサイズを引く
    // --> 現在のキャッシュ・ファイルに追加するので、既存ファイルのサイズが開始点となる

    const auto requiredSizeBytes = min(fileSizeToRead, (FILEIO_LENGTH_T)ctx->mFileInfoRef->FileSize) - cacheFileSize;

    traceW(L"requiredSizeBytes=%lld", requiredSizeBytes);

    // 分割取得する領域を作成

    const auto numParts = (int)((requiredSizeBytes + BYTE_PART_SIZE - 1) / BYTE_PART_SIZE);

    traceW(L"numParts=%d", numParts);

    std::list<std::shared_ptr<FilePart>> fileParts;

    for (int i=0; i<numParts; i++)
    {
        // 分割サイズごとに FilePart を作成
        // このとき、実際のファイル・サイズより大きな範囲を SetRange に指定することになるが
        // レスポンスされるのはファイル・サイズまでなので問題はない

        const auto partOffset = cacheFileSize + BYTE_PART_SIZE * i;

        traceW(L"partOffset[%d]=%lld", i, partOffset);

        fileParts.emplace_back(std::make_shared<FilePart>(partOffset, (ULONG)BYTE_PART_SIZE));
    }

    // マルチパートの読み込みを遅延タスクに登録

    auto* const worker = that->getWorker(L"delayed");

    for (auto& filePart: fileParts)
    {
        auto task{ new ReadPartTask{ that->mDevice, *ctx->mOptObjKey, cacheFilePath, filePart } };
        APP_ASSERT(task);

        worker->addTask(task);
    }

    // タスクの完了を待機

    FILEIO_LENGTH_T sumReadBytes = 0;
    bool errorExists = false;

    for (auto& filePart: fileParts)
    {
        traceW(L"wait: mOffset=%lld", filePart->mOffset);

        const auto reason = ::WaitForSingleObject(filePart->getEvent(), INFINITE);
        APP_ASSERT(reason == WAIT_OBJECT_0);

        if (filePart->isError())
        {
            // エラーがあるパートを発見

            traceW(L"isError: mOffset=%lld", filePart->mOffset);

            errorExists = true;
            break;
        }

        // パートごとに読み取ったサイズを集計

        const auto readBytes = filePart->getResult();

        traceW(L"readBytes=%lld", readBytes);

        sumReadBytes += readBytes;
    }

    if (errorExists)
    {
        // マルチパートの一部にエラーが存在したので、全ての遅延タスクを中断して終了

        for (auto& filePart: fileParts)
        {
            // 全てのパートに中断フラグを立てる

            traceW(L"set mInterrupt mOffset=%lld", filePart->mOffset);

            filePart->mInterrupt = true;
        }

        for (auto& filePart: fileParts)
        {
            // タスクの完了を待機

            const auto reason = ::WaitForSingleObject(filePart->getEvent(), INFINITE);
            APP_ASSERT(reason == WAIT_OBJECT_0);

            if (filePart->isError())
            {
                traceW(L"isError: mOffset=%lld result=%lld", filePart->mOffset, filePart->getResult());
            }
        }

        traceW(L"error exists");
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    // Read 範囲を満たしているかチェック

    if (sumReadBytes < requiredSizeBytes)
    {
        traceW(L"The data is insufficient");
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    // タイムスタンプを同期

    FileHandle file = ::CreateFileW(
        cacheFilePath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        traceW(L"fault: CreateFileW");
        return FspNtStatusFromWin32(::GetLastError());
    }

    if (!syncFileTimes(file.handle(), *ctx->mFileInfoRef))
    {
        traceW(L"fault: syncFileTime");
        const auto lerr = ::GetLastError();
        return FspNtStatusFromWin32(lerr);
    }

    return STATUS_SUCCESS;

}   // syncContent

}   // namespace CSEDRV

// EOF