#include "AwsS3.hpp"
#include "AwsS3_obj_read.h"
#include <filesystem>


using namespace WinCseLib;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
bool AwsS3::readFile_Simple(CALLER_ARG PVOID UParam,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    APP_ASSERT(UParam);

    ReadFileContext* ctx = (ReadFileContext*)UParam;
    APP_ASSERT(!ctx->mBucket.empty());
    APP_ASSERT(!ctx->mKey.empty());
    APP_ASSERT(ctx->mKey.back() != L'/');

    NEW_LOG_BLOCK();

    const auto remotePath{ ctx->getGuardString() };
    traceW(L"HANDLE=%p, Offset=%llu Length=%lu remotePath=%s", ctx->mFile, Offset, Length, remotePath.c_str());

    //
    // 同時に複数のスレッドから異なるオフセットで呼び出されるので
    // 既に mFile に設定されているか判定し、無駄なロックは避ける
    //
    if (ctx->mFile == INVALID_HANDLE_VALUE)
    {
        // ファイル名への参照を登録

        UnprotectedNamedData<Shared_Simple> unsafeShare(remotePath);
        
        {
            // ファイル名のロック
            //
            // 複数スレッドから同一ファイルへの同時アクセスは行われない
            // --> ファイルを安全に操作できることを保証

            ProtectedNamedData<Shared_Simple> safeShare(unsafeShare);

            //
            // 関数先頭でも mFile のチェックをしているが、ロック有無で状況が
            // 変わってくるため、改めてチェックする
            //
            if (ctx->mFile == INVALID_HANDLE_VALUE)
            {
                traceW(L"init mFile: HANDLE=%p, Offset=%llu Length=%lu remotePath=%s",
                    ctx->mFile, Offset, Length, remotePath.c_str());

                // openFile() 後の初回の呼び出し

                const std::wstring localPath{ mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(remotePath) };

                // ダウンロードが必要か判断

                bool needGet = false;

                if (!shouldDownload(CONT_CALLER ctx->mBucket, ctx->mKey, localPath, &ctx->mFileInfo, &needGet))
                {
                    traceW(L"fault: shouldDownload");
                    return false;
                }

                traceW(L"needGet: %s", needGet ? L"true" : L"false");

                if (needGet)
                {
                    // キャッシュ・ファイルの準備

                    const FileOutputMeta meta{ localPath, CREATE_ALWAYS, false, 0, 0, true };

                    const auto bytesWritten = this->prepareLocalCacheFile(CONT_CALLER
                        ctx->mBucket, ctx->mKey, meta);

                    if (bytesWritten < 0)
                    {
                        traceW(L"fault: prepareLocalCacheFile_Simple return=%lld", bytesWritten);
                        return false;
                    }
                }

                APP_ASSERT(std::filesystem::exists(localPath));

                // キャッシュ・ファイルを開き、HANDLE をコンテキストに保存

                ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
                if (ctx->mCreateOptions & FILE_DELETE_ON_CLOSE)
                    CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

#if 0
                traceW(L"CreateFile path=%s dwDesiredAccess=%ld dwFlagsAndAttributes=%ld",
                    localPath.c_str(), ctx->mGrantedAccess, CreateFlags);

                traceW(L"compare dwDesiredAccess=%ld dwFlagsAndAttributes=%ld",
                    FILE_READ_DATA | FILE_WRITE_DATA | FILE_WRITE_EA | FILE_READ_ATTRIBUTES | GENERIC_ALL,
                    FILE_FLAG_BACKUP_SEMANTICS);
#endif
                ctx->mFile = ::CreateFileW(localPath.c_str(),
                    ctx->mGrantedAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, CreateFlags, NULL);

                if (ctx->mFile == INVALID_HANDLE_VALUE)
                {
                    traceW(L"fault: CreateFileW");
                    return false;
                }

                StatsIncr(_CreateFile);

                // 属性情報のサイズと比較

                LARGE_INTEGER fileSize;
                if(!::GetFileSizeEx(ctx->mFile, &fileSize))
                {
                    traceW(L"fault: GetFileSizeEx");
                    return false;
                }

                if (ctx->mFileInfo.FileSize != (UINT64)fileSize.QuadPart)
                {
                    traceW(L"fault: no match filesize ");
                    return false;
                }
            }

            // ファイル名のロックを解放
        }

        // ファイル名への参照を解除
    }

    APP_ASSERT(ctx->mFile);
    APP_ASSERT(ctx->mFile != INVALID_HANDLE_VALUE);

    // Offset, Length によりファイルを読む

    OVERLAPPED Overlapped{};
    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::ReadFile(ctx->mFile, Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const DWORD lerr = ::GetLastError();
        traceW(L"fault: ReadFile LastError=%ld", lerr);

        return false;
    }

    traceW(L"success: HANDLE=%p, Offset=%llu Length=%lu, PBytesTransferred=%lu, diffOffset=%llu",
        ctx->mFile, Offset, Length, *PBytesTransferred, Offset - ctx->mLastOffset);

    ctx->mLastOffset = Offset;

    return true;
}

// EOF