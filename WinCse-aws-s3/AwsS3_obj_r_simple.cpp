#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
NTSTATUS AwsS3::readObject_Simple(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s", ctx->mObjKey.c_str());

    const auto remotePath{ ctx->mObjKey.str() };

    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
        ctx, ctx->mFile.handle(), Offset, Length, remotePath.c_str());

    // ファイル名への参照を登録

    UnprotectedShare<CreateFileShared> unsafeShare(&mGuardCreateFile, remotePath);  // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() 後の初回の呼び出し

            traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                ctx->mFile.handle(), Offset, Length, remotePath.c_str());

            std::wstring localPath;

            if (!ctx->getCacheFilePath(&localPath))
            {
                traceW(L"fault: getCacheFilePath");
                //return STATUS_OBJECT_NAME_NOT_FOUND;
                return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
            }

            // ダウンロードが必要か判断

            bool needDownload = false;

            NTSTATUS ntstatus = syncFileAttributes(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: syncFileAttributes");
                return ntstatus;
            }

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

            if (needDownload)
            {
                // キャッシュ・ファイルの準備

                const FileOutputParams outputParams
                {
                    localPath,
                    CREATE_ALWAYS,
                    false,              // SetRange()
                    0,                  // Offset
                    0                   // Length
                };

                const auto bytesWritten = this->prepareLocalCacheFile(CONT_CALLER ctx->mObjKey, outputParams);

                if (bytesWritten < 0)
                {
                    traceW(L"fault: prepareLocalCacheFile_Simple bytesWritten=%lld", bytesWritten);
                    //return STATUS_IO_DEVICE_ERROR;
                    return FspNtStatusFromWin32(ERROR_IO_DEVICE);
                }
            }
            else
            {
                if (ctx->mFileInfo.FileSize == 0)
                {
                    //return STATUS_END_OF_FILE;
                    return FspNtStatusFromWin32(ERROR_HANDLE_EOF);
                }
            }

            // 既存のファイルを開く

            ntstatus = ctx->openFileHandle(CONT_CALLER
                //needDownload ? FILE_WRITE_ATTRIBUTES : 0,
                FILE_WRITE_ATTRIBUTES,
                OPEN_EXISTING
            );

            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: openFileHandle");
                return ntstatus;
            }

            APP_ASSERT(ctx->mFile.valid());

            if (needDownload)
            {
#if SET_ATTRIBUTES_LOCAL_FILE
                // ファイル属性を同期

                if (!ctx->mFile.setBasicInfo(ctx->mFileInfo))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setBasicInfo lerr=%lu", lerr);

                    return FspNtStatusFromWin32(lerr);
                }

#else
                // ファイル日付の同期

                if (!ctx->mFile.setFileTime(ctx->mFileInfo))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setBasicInfo lerr=%lu", lerr);

                    return FspNtStatusFromWin32(lerr);
                }

#endif
            }
            else
            {
                // アクセス日時のみ更新

                if (!ctx->mFile.setFileTime(0, 0))
                {
                    const auto lerr = ::GetLastError();
                    traceW(L"fault: setFileTime lerr=%lu", lerr);

                    return FspNtStatusFromWin32(lerr);
                }
            }

            // 属性情報のサイズと比較

            LARGE_INTEGER fileSize;
            if(!::GetFileSizeEx(ctx->mFile.handle(), &fileSize))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

                return FspNtStatusFromWin32(lerr);
            }

            if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
            {
                APP_ASSERT(0);

                traceW(L"fault: no match filesize ");
                //return STATUS_IO_DEVICE_ERROR;
                return FspNtStatusFromWin32(ERROR_IO_DEVICE);
            }
        }
    }   // 名前のロックを解除 (safeShare の生存期間)

    APP_ASSERT(ctx->mFile.valid());

    // Offset, Length によりファイルを読む

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: ReadFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mFile.handle(), Offset, Length, *PBytesTransferred);

    return STATUS_SUCCESS;
}

// EOF