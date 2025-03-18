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

bool AwsS3::readObject_Simple(CALLER_ARG WinCseLib::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx->isFile());

    NEW_LOG_BLOCK();

    bool ret = false;
    OVERLAPPED Overlapped{};

    const auto remotePath{ ctx->getRemotePath() };
    traceW(L"ctx=%p HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
        ctx, ctx->mLocalFile.handle(), Offset, Length, remotePath.c_str());

    UnprotectedShare<Shared> unsafeShare(&gSharedStore, remotePath);                // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        //
        // 関数先頭でも mLocalFile のチェックをしているが、ロック有無で状況が
        // 変わってくるため、改めてチェックする
        //
        if (ctx->mLocalFile.invalid())
        {
            traceW(L"init mLocalFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                ctx->mLocalFile.handle(), Offset, Length, remotePath.c_str());

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

                    if (!ctx->openLocalFile(FILE_WRITE_ATTRIBUTES, CREATE_ALWAYS))
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

                ret = true;
                goto exit;
            }

            // ダウンロードが必要か判断

            bool needDownload = false;

            if (!shouldDownload(CONT_CALLER ctx->mObjKey, ctx->mFileInfo, localPath, &needDownload))
            {
                traceW(L"fault: shouldDownload");
                goto exit;
            }

            traceW(L"needDownload: %s", BOOL_CSTRW(needDownload));

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

            APP_ASSERT(ctx->mLocalFile.valid());

            // 属性情報のサイズと比較

            LARGE_INTEGER fileSize;
            if(!::GetFileSizeEx(ctx->mLocalFile.handle(), &fileSize))
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
    }   // 名前のロックを解除 (safeShare の生存期間)

    APP_ASSERT(ctx->mLocalFile.valid());

    // Offset, Length によりファイルを読む

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mLocalFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        goto exit;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mLocalFile.handle(), Offset, Length, *PBytesTransferred);

    ret = true;

exit:
    traceW(L"ret=%s", BOOL_CSTRW(ret));

    return ret;
}

// EOF