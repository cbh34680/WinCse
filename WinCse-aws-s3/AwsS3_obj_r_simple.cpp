#include "AwsS3.hpp"
#include <filesystem>


using namespace WinCseLib;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
struct Shared : public SharedBase { };
static ShareStore<Shared> gSharedStore;

NTSTATUS AwsS3::readObject_Simple(CALLER_ARG WinCseLib::IOpenContext* argOpenContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);
    APP_ASSERT(ctx->isFile());

    NEW_LOG_BLOCK();

    NTSTATUS ntstatus = STATUS_IO_DEVICE_ERROR;
    OVERLAPPED Overlapped{};

    const auto remotePath{ ctx->getRemotePath() };
    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx, ctx->mLocalFile, Offset, Length, remotePath.c_str());

    {
        // ファイル名への参照を登録

        UnprotectedShare<Shared> unsafeShare(&gSharedStore, remotePath);
        
        {
            // ファイル名のロック
            //
            // 複数スレッドから同一ファイルへの同時アクセスは行われない
            // --> ファイルを安全に操作できることを保証

            ProtectedShare<Shared> safeShare(&unsafeShare);

            //
            // 関数先頭でも mLocalFile のチェックをしているが、ロック有無で状況が
            // 変わってくるため、改めてチェックする
            //
            if (ctx->mLocalFile == INVALID_HANDLE_VALUE)
            {
                traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                    ctx->mLocalFile, Offset, Length, remotePath.c_str());

                // openFile() 後の初回の呼び出し

                const std::wstring localPath{ ctx->getLocalPath() };

                if (ctx->mFileInfo.FileSize == 0)
                {
                    // ファイルが空なのでダウンロードは不要

                    const auto alreadyExists = std::filesystem::exists(localPath);

                    if (!alreadyExists)
                    {
                        // ローカルに存在しないので touch と同義

                        // タイムスタンプを属性情報に合わせる
                        // SetFileTime を実行するので、GENERIC_WRITE が必要

                        if (!ctx->openLocalFile(GENERIC_WRITE, CREATE_ALWAYS))
                        {
                            traceW(L"fault: openFile");
                            goto exit;
                        }

                        if (!ctx->setLocalFileTime(ctx->mFileInfo.CreationTime))
                        {
                            traceW(L"fault: setLocalTimeTime");
                            goto exit;
                        }
                    }

                    // ファイルが空なので、EOF を返却

                    ntstatus = STATUS_END_OF_FILE;
                    goto exit;
                }

                // ダウンロードが必要か判断

                bool needDownload = false;

                if (!shouldDownload(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
                {
                    traceW(L"fault: shouldDownload");
                    goto exit;
                }

                traceW(L"needDownload: %s", needDownload ? L"true" : L"false");

                if (needDownload)
                {
                    // キャッシュ・ファイルの準備

                    const FileOutputMeta meta
                    {
                        localPath,
                        CREATE_ALWAYS,
                        false,              // SetRange()
                        0,                  // Offset
                        0,                  // Length
                        true                // SetFileTime()
                    };

                    const auto bytesWritten = this->prepareLocalCacheFile(CONT_CALLER ctx->mObjKey, meta);

                    if (bytesWritten < 0)
                    {
                        traceW(L"fault: prepareLocalCacheFile_Simple bytesWritten=%lld", bytesWritten);
                        goto exit;
                    }
                }

                // 既存のファイルを開く

                APP_ASSERT(std::filesystem::exists(localPath));

                if (!ctx->openLocalFile(0, OPEN_EXISTING))
                {
                    traceW(L"fault: openFile");
                    goto exit;
                }

                APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

                // 属性情報のサイズと比較

                LARGE_INTEGER fileSize;
                if(!::GetFileSizeEx(ctx->mLocalFile, &fileSize))
                {
                    traceW(L"fault: GetFileSizeEx");
                    goto exit;
                }

                if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
                {
                    traceW(L"fault: no match filesize ");
                    goto exit;
                }
            }

            // ファイル名のロックを解放
        }

        // ファイル名への参照を解除
    }

    APP_ASSERT(ctx->mLocalFile);
    APP_ASSERT(ctx->mLocalFile != INVALID_HANDLE_VALUE);

    // Offset, Length によりファイルを読む

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mLocalFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mLocalFile, Offset, Length, *PBytesTransferred);

    ntstatus = STATUS_SUCCESS;

exit:
    traceW(L"ntstatus=%ld", ntstatus);

    return ntstatus;
}

// EOF