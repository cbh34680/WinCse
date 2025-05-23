#include "CSDriver.hpp"

using namespace CSELIB;

static bool syncFileTimes(const FSP_FSCTL_FILE_INFO& fileInfo, HANDLE hFile)
{
    FILETIME ftCreation;
    FILETIME ftLastAccess;
    FILETIME ftLastWrite;

    WinFileTime100nsToWinFile(fileInfo.CreationTime, &ftCreation);
    ::GetSystemTimeAsFileTime(&ftLastAccess);
    WinFileTime100nsToWinFile(fileInfo.LastWriteTime, &ftLastWrite);

    return ::SetFileTime(hFile, &ftCreation, &ftLastAccess, &ftLastWrite);
}

using ReadFilePartType = CSELIB::FilePart<FILEIO_LENGTH_T>;

struct ReadFilePartTask : public IOnDemandTask
{
    ICSDevice* mThat;
    const ObjectKey mObjKey;
    const std::filesystem::path mOutputPath;
    std::shared_ptr<ReadFilePartType> mFilePart;

    ReadFilePartTask(
        ICSDevice* argThat,
        const ObjectKey& argObjKey,
        const std::filesystem::path& argOutputPath,
        const std::shared_ptr<ReadFilePartType>& argFilePart)
        :
        mThat(argThat),
        mObjKey(argObjKey),
        mOutputPath(argOutputPath),
        mFilePart(argFilePart)
    {
    }

    void run(int argThreadIndex) override
    {
        NEW_LOG_BLOCK();

        FILEIO_LENGTH_T result = -1LL;

        try
        {
            if (mFilePart->mInterrupt)
            {
                errorW(L"@%d Interruption request received", argThreadIndex);
            }
            else
            {
                traceW(L"@%d getObjectAndWriteFile", argThreadIndex);

                result = mThat->getObjectAndWriteFile(START_CALLER mObjKey, mOutputPath, mFilePart->mOffset, mFilePart->mLength);
            }
        }
        catch (const std::exception& ex)
        {
            errorA("catch exception: what=[%s]", ex.what());
        }
        catch (...)
        {
            errorW(L"catch unknown");
        }

        // 結果を設定し、シグナル状態に変更
        // --> WaitForSingleObject で待機しているスレッドのロックが解除される

        mFilePart->setResult(result);
    }
};

namespace CSEDRV {

bool resolveCacheFilePath(const std::filesystem::path& argDir, const std::wstring& argWinPath, std::filesystem::path* pPath)
{
    NEW_LOG_BLOCK();

    if (!std::filesystem::is_directory(argDir))
    {
        errorW(L"fault: is_directory argDir=%s", argDir.c_str());
        return false;
    }

    std::wstring nameSha256;

    const auto ntstatus = ComputeSHA256W(argWinPath, &nameSha256);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: ComputeSHA256W argWinPath=%s", argWinPath.c_str());
        return false;
    }

    // 先頭の 2Byte はディレクトリ名

    auto filePath{ argDir / SafeSubStringW(nameSha256, 0, 2) };

    std::error_code ec;
    std::filesystem::create_directory(filePath, ec);

    if (ec)
    {
        errorW(L"fault: create_directory filePath=%s", filePath.c_str());
        return false;
    }

    filePath.append(SafeSubStringW(nameSha256, 2));

    *pPath = std::move(filePath);

    return true;
}

NTSTATUS syncAttributes(const DirEntryType& remoteDirEntry, const std::filesystem::path& cacheFilePath)
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
        const auto lerr = ::GetLastError();

        errorW(L"fault: CreateFileW lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
        return FspNtStatusFromWin32(lerr);
    }

    FSP_FSCTL_FILE_INFO localInfo;
    const auto ntstatus = GetFileInfoInternal(file.handle(), &localInfo);

    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: GetFileInfoInternal file=%s", file.str().c_str());
        return ntstatus;
    }

    if (localInfo.CreationTime  == remoteDirEntry->mFileInfo.CreationTime &&
        localInfo.LastWriteTime == remoteDirEntry->mFileInfo.LastWriteTime)
    {
        // 二つのタイムスタンプが同じときは同期中と考える

        traceW(L"In sync");
    }
    else
    {
        // タイムスタンプが異なるときは、ダウンロードを促すためにファイルを切り詰める

        if (localInfo.FileSize > 0)
        {
            // ファイルポインタを先頭に移動 (必要ないけど、念のため)

            if (::SetFilePointer(file.handle(), 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
            {
                const auto lerr = ::GetLastError();

                errorW(L"fault: SetFilePointer lerr=%lu file=%s", lerr, file.str().c_str());
                return FspNtStatusFromWin32(lerr);
            }

            // ファイルを切り詰める

            if (!::SetEndOfFile(file.handle()))
            {
                const auto lerr = ::GetLastError();

                errorW(L"fault: SetEndOfFile lerr=%lu file=%s", lerr, file.str().c_str());
                return FspNtStatusFromWin32(lerr);
            }
        }

        // タイムスタンプを同期

        if (!syncFileTimes(remoteDirEntry->mFileInfo, file.handle()))
        {
            const auto lerr = ::GetLastError();

            errorW(L"fault: syncFileTime lerr=%lu file=%s", lerr, file.str().c_str());
            return FspNtStatusFromWin32(lerr);
        }
    }

    return STATUS_SUCCESS;

}   // syncAttributes

NTSTATUS CSDriver::updateFileInfo(CALLER_ARG FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo, bool argRemoteSizeAware)
{
    NEW_LOG_BLOCK();

    // キャッシュファイルの情報を取得

    FSP_FSCTL_FILE_INFO cacheFileInfo;

    const auto ntstatus = GetFileInfoInternal(ctx->getWritableHandle(), &cacheFileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: GetFileInfoInternal ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    const auto& dirEntry{ ctx->getDirEntry() };

    if (argRemoteSizeAware)
    {
        if (cacheFileInfo.FileSize < dirEntry->mFileInfo.FileSize)
        {
            // ダウンロードされていない部分もあるので、サイズはリモートの情報を上書き

            cacheFileInfo.FileSize       = dirEntry->mFileInfo.FileSize;
            cacheFileInfo.AllocationSize = dirEntry->mFileInfo.AllocationSize;
        }
    }

    // ctx を経由し、OpenDirEntry の mFileInfo を更新する

    dirEntry->mFileInfo = cacheFileInfo;
    *pFileInfo          = cacheFileInfo;

    return STATUS_SUCCESS;
}

NTSTATUS CSDriver::syncContent(CALLER_ARG FileContext* ctx, FILEIO_OFFSET_T argReadOffset, FILEIO_LENGTH_T argReadLength)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s argReadOffset=%lld argReadLength=%lld", ctx->str().c_str(), argReadOffset, argReadLength);

    if (argReadLength == 0)
    {
        traceW(L"Empty read");
        return STATUS_SUCCESS;
    }

    const auto& fileInfo{ ctx->getDirEntry()->mFileInfo };

    if (fileInfo.FileSize == 0)
    {
        traceW(L"Empty content");
        return STATUS_SUCCESS;
    }

    // ファイル・ハンドルからローカルのファイル名を取得

    std::filesystem::path filePath;

    if (!GetFileNameFromHandle(ctx->getHandle(), &filePath))
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: GetFileNameFromHandle lerr=%lu", lerr);
        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"filePath=%s", filePath.c_str());

    // ファイルサイズを取得

    const auto fileSize = GetFileSize(filePath);
    if (fileSize < 0)
    {
        errorW(L"fault: getFileSize");
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"fileSize=%lld", fileSize);

    // ファイルサイズとリモートの属性情報を比較

    if (fileSize >= (FILESIZE_T)fileInfo.FileSize)
    {
        // 全てダウンロード済なので OK
        // 
        // --> ファイルを切り詰めた場合はディレクトリエントリも変更している

        traceW(L"All content has been downloaded");
        return STATUS_SUCCESS;
    }

    // ファイル・サイズと Read 対象範囲を比較

    FILEIO_LENGTH_T fileSizeToRead = argReadOffset + argReadLength;

    if (fileSize >= fileSizeToRead)
    {
        // Read 範囲のデータは存在するので OK

        traceW(L"Download not required");
        return STATUS_SUCCESS;
    }

    traceW(L"fileSizeToRead=%lld", fileSizeToRead);

    // Read 範囲のデータが不足しているのでダウンロードを実施

    APP_ASSERT(fileSize < static_cast<FILESIZE_T>(fileInfo.FileSize));
    APP_ASSERT(fileSize < fileSizeToRead);

    // マルチパート処理ののパートサイズ

#if 0
    traceW(L"!!");
    traceW(L"!! WARNING: PART SIZE !!");
    traceW(L"!!");

    const auto PART_SIZE_BYTE = ILESIZE_1Bll * 10;

#else
    auto PART_SIZE_BYTE = FILESIZE_1MiBll * mRuntimeEnv->TransferReadSizeMib;

    if (argReadOffset == 0 && fileSizeToRead <= FILESIZE_1MiBll)
    {
        // エクスプローラでプロパティを開くとメタデータが読み取られることに対応
        // --> 先頭の 1MiB までの Read の場合はパートサイズを 1MiB に設定

        PART_SIZE_BYTE = FILESIZE_1MiBll;
    }

#endif
    traceW(L"PART_SIZE_BYTE=%lld", PART_SIZE_BYTE);

    const auto& objKey{ ctx->getObjectKey() };

    // アライメントサイズに調整

    auto alignedFileSizeToRead = ALIGN_TO_UNIT(fileSizeToRead, PART_SIZE_BYTE);

    if (static_cast<FILESIZE_T>(fileInfo.FileSize) < alignedFileSizeToRead)
    {
        // リモートのサイズを Read 対象の上限とする

        alignedFileSizeToRead = fileInfo.FileSize;
    }

    APP_ASSERT(alignedFileSizeToRead <= static_cast<FILESIZE_T>(fileInfo.FileSize));

    // 各変数の値の関係性
    // 
    // [fileSize] < [fileSizeToRead] <= [alignedFileSizeToRead] <= [fileInfo.FileSize]

    // 必要となるサイズ

    const auto requiredSizeBytes = alignedFileSizeToRead - fileSize;

    traceW(L"requiredSizeBytes=%lld", requiredSizeBytes);

    // 分割取得する領域を作成

    const auto partCount = UNIT_COUNT(requiredSizeBytes, PART_SIZE_BYTE);

    std::list<std::shared_ptr<ReadFilePartType>> fileParts;

    auto remaining = requiredSizeBytes;
        
    for (int i=0; i<partCount; i++)
    {
        // 分割サイズごとに FilePart を作成

        const auto partNumber = i + 1;
        const auto partOffset = fileSize + PART_SIZE_BYTE * i;
        const auto partLength = min(PART_SIZE_BYTE, remaining);

        fileParts.emplace_back(std::make_shared<ReadFilePartType>(partNumber, partOffset, partLength, -1LL));

        remaining -= partLength;
    }

    if (fileParts.size() == 1)
    {
        const auto& filePart{ *fileParts.begin() };

        // 一度で全て読めてしまうので複雑なことはしない

        const auto readBytes = mDevice->getObjectAndWriteFile(START_CALLER objKey, filePath, filePart->mOffset, filePart->mLength);

        if (filePart->mLength != readBytes)
        {
            errorW(L"fault: getObjectAndWriteFile mLength=%lld readBytes=%lld", filePart->mLength, readBytes);

            return FspNtStatusFromWin32(ERROR_IO_DEVICE);
        }
    }
    else
    {
        // マルチパートの読み込みを遅延タスクに登録

        auto* const worker = this->getWorker(L"delayed");

        for (const auto& filePart: fileParts)
        {
            traceW(L"addTask filePart=%s", filePart->str().c_str());

            worker->addTask(new ReadFilePartTask{ mDevice, objKey, filePath, filePart });
        }

        // タスクの完了を待機

        FILEIO_LENGTH_T sumReadBytes = 0;

        for (const auto& filePart: fileParts)
        {
            // パートごとに読み取ったサイズを集計

            const auto result = filePart->getResult();

            traceW(L"getResult filePart=%s result=%lld", filePart->str().c_str(), result);

            if (result != filePart->mLength)
            {
                sumReadBytes = -1LL;

                errorW(L"fault: mPartNumber=%d", filePart->mPartNumber);
                break;
            }

            sumReadBytes += result;
        }

        if (sumReadBytes < requiredSizeBytes)
        {
            // マルチパートの一部にエラーが存在したので、全ての遅延タスクを中断して終了

            errorW(L"The data is insufficient sumReadBytes=%lld", sumReadBytes);

            for (auto& filePart: fileParts)
            {
                // 全てのパートに中断フラグを立てる

                traceW(L"set mInterrupt mPartNumber=%lld", filePart->mPartNumber);

                filePart->mInterrupt = true;
            }

            for (auto& filePart: fileParts)
            {
                // タスクの完了を待機

                const auto result = filePart->getResult();
                if (result != filePart->mLength)
                {
                    errorW(L"fault: mPartNumber=%d", filePart->mPartNumber);
                }
            }

            traceW(L"error exists");
            return FspNtStatusFromWin32(ERROR_IO_DEVICE);
        }
    }

    // タイムスタンプを同期

    FileHandle file = ::CreateFileW(
        filePath.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file.invalid())
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: CreateFileW lerr=%lu filePath=%s", lerr, filePath.c_str());
        return FspNtStatusFromWin32(lerr);
    }

    if (!syncFileTimes(fileInfo, file.handle()))
    {
        const auto lerr = ::GetLastError();

        errorW(L"fault: syncFileTime file=%s", file.str().c_str());
        return FspNtStatusFromWin32(lerr);
    }

    return STATUS_SUCCESS;

}   // syncContent

}   // namespace CSEDRV

// EOF