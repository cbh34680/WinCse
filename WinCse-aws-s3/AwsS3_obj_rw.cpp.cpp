#include "AwsS3.hpp"
#include <fstream>

using namespace WCSE;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
NTSTATUS AwsS3::readObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    StatsIncr(readObject);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s, ctx=%p HANDLE=%p, Offset=%llu, Length=%lu",
        ctx->mObjKey.c_str(), ctx, ctx->mFile.handle(), Offset, Length);

    const auto ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: prepareLocalFile");
        return ntstatus;
    }

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

    traceW(L"PBytesTransferred=%lu", *PBytesTransferred);

    return STATUS_SUCCESS;
}

NTSTATUS AwsS3::writeObject(CALLER_ARG WCSE::CSDeviceContext* argCSDeviceContext,
    PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s, ctx=%p HANDLE=%p, Offset=%llu, Length=%lu, WriteToEndOfFile=%s, ConstrainedIo=%s",
        ctx->mObjKey.c_str(), ctx, ctx->mFile.handle(), Offset, Length,
        BOOL_CSTRW(WriteToEndOfFile), BOOL_CSTRW(ConstrainedIo));

    auto ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: prepareLocalFile");
        return ntstatus;
    }

    APP_ASSERT(ctx->mFile.valid());

    if (ConstrainedIo)
    {
        LARGE_INTEGER FileSize;

        if (!::GetFileSizeEx(ctx->mFile.handle(), &FileSize))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: GetFileSizeEx lerr=%lu", lerr);

            return FspNtStatusFromWin32(lerr);
        }

        if (Offset >= (UINT64)FileSize.QuadPart)
        {
            return STATUS_SUCCESS;
        }

        if (Offset + Length > (UINT64)FileSize.QuadPart)
        {
            Length = (ULONG)((UINT64)FileSize.QuadPart - Offset);
        }
    }

    OVERLAPPED Overlapped{};

    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);

    if (!::WriteFile(ctx->mFile.handle(), Buffer, Length, PBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: WriteFile lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"PBytesTransferred=%lu", *PBytesTransferred);

    ctx->mFlags |= CSDCTX_FLAGS_WRITE;

    ntstatus = GetFileInfoInternal(ctx->mFile.handle(), FileInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: GetFileInfoInternal");
        return FspNtStatusFromWin32(::GetLastError());
    }

    return STATUS_SUCCESS;
}

bool AwsS3::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
{
    NEW_LOG_BLOCK();

    // 先にディレクトリ内のファイルから削除する
    // --> サブディレクトリは含まれていないはず

    if (argObjKey.meansDir())
    {
        while (1)
        {
            //
            // 一度の listObjects では最大数の制限があるかもしれないので、削除する
            // 対象がなくなるまで繰り返す
            //

            DirInfoListType dirInfoList;

            if (!this->listObjects(CONT_CALLER argObjKey, &dirInfoList))
            {
                traceW(L"fault: listObjects");
                return false;
            }

            Aws::S3::Model::Delete delete_objects;

            for (const auto& dirInfo: dirInfoList)
            {
                if (wcscmp(dirInfo->FileNameBuf, L".") == 0 || wcscmp(dirInfo->FileNameBuf, L"..") == 0)
                {
                    continue;
                }

                if (FA_IS_DIRECTORY(dirInfo->FileInfo.FileAttributes))
                {
                    // 削除開始からここまでの間にディレクトリが作成される可能性を考え
                    // 存在したら無視

                    continue;
                }

                const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

                Aws::S3::Model::ObjectIdentifier obj;
                obj.SetKey(fileObjKey.keyA());
                delete_objects.AddObjects(obj);

                traceW(L"delete_objects.AddObjects fileObjKey=%s", fileObjKey.c_str());

                // ローカルのキャッシュ・ファイルを削除

                const std::wstring localPath{ GetCacheFilePath(mCacheDataDir, fileObjKey.str()) };

                if (::DeleteFileW(localPath.c_str()))
                {
                    traceW(L"success DeleteFileW localPath=%s", localPath.c_str());
                }
                else
                {
                    const auto lerr = ::GetLastError();
                    if (lerr != ERROR_FILE_NOT_FOUND)
                    {
                        traceW(L"fault: DeleteFileW, lerr=%lu", lerr);
                        return false;
                    }
                }

                // キャッシュ・メモリから削除 (ファイル)

                const auto num = deleteObjectCache(CONT_CALLER fileObjKey);
                traceW(L"cache delete num=%d, fileObjKey=%s", num, fileObjKey.c_str());
            }

            if (delete_objects.GetObjects().empty())
            {
                break;
            }

            traceW(L"DeleteObjects bucket=%s size=%zu", argObjKey.bucket().c_str(), delete_objects.GetObjects().size());

            Aws::S3::Model::DeleteObjectsRequest request;
            request.SetBucket(argObjKey.bucketA());
            request.SetDelete(delete_objects);

            const auto outcome = mClient->DeleteObjects(request);

            if (!outcomeIsSuccess(outcome))
            {
                traceW(L"fault: DeleteObjects");
                return false;
            }
        }
    }

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    const auto outcome = mClient->DeleteObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    if (argObjKey.meansDir())
    {
        // 新規作成時に作られたローカル・ディレクトリが存在したら削除

        const std::wstring localPath{ GetCacheFilePath(mCacheDataDir, argObjKey.str()) };

        if (::RemoveDirectoryW(localPath.c_str()))
        {
            traceW(L"success RemoveDirectoryW localPath=%s", localPath.c_str());
        }
        else
        {
            const auto lerr = ::GetLastError();
            if (lerr != ERROR_FILE_NOT_FOUND)
            {
                traceW(L"fault: RemoveDirectoryW, lerr=%lu", lerr);
                //return false;
            }
        }
    }

    // キャッシュ・メモリから削除 (ディレクトリ)

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

bool AwsS3::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, const std::wstring& argFilePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argFilePath=%s", argObjKey.c_str(), argFilePath.c_str());

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (FA_IS_DIRECTORY(argFileInfo.FileAttributes))
    {
        // ディレクトリの場合は空のコンテンツ
    }
    else
    {
        // ファイルの場合はローカル・キャッシュの内容をアップロードする

        const Aws::String filePath{ WC2MB(argFilePath) };

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>
        (
            __FUNCTION__,
            filePath.c_str(),
            std::ios_base::in | std::ios_base::binary
        );

        if (!inputData->good())
        {
            const auto lerr = ::GetLastError();

            traceW(L"fault: inputData->good, fail=%s bad=%s, eof=%s, lerr=%lu",
                BOOL_CSTRW(inputData->fail()), BOOL_CSTRW(inputData->bad()), BOOL_CSTRW(inputData->eof()), lerr);

            return false;
        }

        request.SetBody(inputData);
    }

    const auto sCreationTime{ std::to_string(argFileInfo.CreationTime) };
    const auto sLastWriteTime{ std::to_string(argFileInfo.LastWriteTime) };

    request.AddMetadata("wincse-creation-time", sCreationTime.c_str());
    request.AddMetadata("wincse-last-write-time", sLastWriteTime.c_str());

    traceA("sCreationTime=%s", sCreationTime.c_str());
    traceA("sLastWriteTime=%s", sLastWriteTime.c_str());

#if _DEBUG
    request.AddMetadata("wincse-debug-source-path", WC2MB(argFilePath).c_str());
    request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime).c_str());
    request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime).c_str());
#endif

    traceW(L"PutObject argObjKey=%s, argFilePath=%s", argObjKey.c_str(), argFilePath.c_str());

    const auto outcome = mClient->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    // キャッシュ・メモリから削除
    //
    // 上記で作成したディレクトリがキャッシュに反映されていない状態で
    // 利用されてしまうことを回避するために事前に削除しておき、改めてキャッシュを作成させる

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    // headObject() は必須ではないが、作成直後に属性が参照されることに対応

    if (!headObject(CONT_CALLER argObjKey, nullptr))
    {
        traceW(L"fault: headObject");
    }

    return true;
}

bool AwsS3::renameObject(CALLER_ARG WCSE::CSDeviceContext* ctx, const ObjectKey& argNewObjKey)
{
    NEW_LOG_BLOCK();

    const auto localPath{ ctx->getCacheFilePath() };

    FSP_FSCTL_FILE_INFO localInfo;
    const auto ntstatus = PathToFileInfo(localPath, &localInfo);
    if (!NT_SUCCESS(ntstatus))
    {
        traceW(L"fault: PathToFileInfo, localPath=%s", localPath.c_str());
        return false;
    }

    FSP_FSCTL_FILE_INFO remoteInfo{};

    if (ctx->isFile())
    {
        // ファイルの場合は headObject() の情報が正しいので、ctx のものをそのまま利用

        remoteInfo = ctx->mFileInfo;
    }
    else
    {
        // ディレクトリの場合、親ディレクトリの CommonPrefix を元にした情報になるので
        // HeadObject によりリモートの情報を取得する

        const auto dirInfo{ this->apicallHeadObject(CONT_CALLER ctx->mObjKey) };
        if (!dirInfo)
        {
            traceW(L"fault: apicallHeadObject");
            return false;
        }

        remoteInfo = dirInfo->FileInfo;
    }

    APP_ASSERT(remoteInfo.CreationTime);

    if (localInfo.CreationTime == remoteInfo.CreationTime &&
        localInfo.LastWriteTime == remoteInfo.LastWriteTime &&
        localInfo.FileSize == remoteInfo.FileSize)
    {
        // go next
    }
    else
    {
        traceW(L"no match local:remote");
        traceW(L"localPath=%s", localPath.c_str());
        traceW(L"mObjKey=%s", ctx->mObjKey.c_str());
        traceW(L"localInfo:  CreationTime=%llu, LastWriteTime=%llu, FileSize=%llu", localInfo.CreationTime, localInfo.LastWriteTime, localInfo.FileSize);
        traceW(L"remoteInfo: CreationTime=%llu, LastWriteTime=%llu, FileSize=%llu", remoteInfo.CreationTime, remoteInfo.LastWriteTime, remoteInfo.FileSize);

        // 一致しないローカル・キャッシュは削除する

        if (ctx->isDir())
        {
            if (::RemoveDirectoryW(localPath.c_str()))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: RemoveDirectory, lerr=%lu", lerr);

                // エラーは無視
            }
        }
        else
        {
            if (::DeleteFileW(localPath.c_str()))
            {
                const auto lerr = ::GetLastError();
                traceW(L"fault: DeleteFileW, lerr=%lu", lerr);

                // エラーは無視
            }
        }

        return false;
    }

    // ローカルにファイルが存在し、リモートと完全に一致する状況なので、リネーム処理を実施

    // 新しい名前でアップロードする

    traceW(L"putObject argNewObjKey=%s, localPath=%s", argNewObjKey.c_str(), localPath.c_str());

    if (!this->putObject(CONT_CALLER argNewObjKey, remoteInfo, localPath))
    {
        traceW(L"fault: putObject");

        return false;
    }

    // 古い名前を削除

    if (!this->deleteObject(CONT_CALLER ctx->mObjKey))
    {
        traceW(L"fault: deleteObject");

        return false;
    }

    if (ctx->isDir())
    {
        // ディレクトリ名の変更は、新規作成時のみ
        // --> 削除することで、既存のディレクトリのリネームは関数先頭にある PathToFileInfo で失敗させる

        traceW(L"RemoveDirectory localPath=%s", localPath.c_str());

        if (::RemoveDirectoryW(localPath.c_str()))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: RemoveDirectory, lerr=%lu", lerr);

            // エラーは無視
        }
    }
    else
    {
        // キャッシュ・ファイルのリネーム

        const auto newLocalPath{ GetCacheFilePath(ctx->mCacheDataDir, argNewObjKey.str()) };

        traceW(L"MoveFileExW localPath=%s, newLocalPath=%s", localPath.c_str(), newLocalPath.c_str());

        if (!::MoveFileExW(localPath.c_str(), newLocalPath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: MoveFileExW, lerr=%lu", lerr);

            // エラーは無視
        }
    }

    // キャッシュ・メモリから削除

    const auto num = this->deleteObjectCache(CONT_CALLER argNewObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argNewObjKey.c_str());

    // headObject() は必須ではないが、作成直後に属性が参照されることに対応

    if (!headObject(CONT_CALLER argNewObjKey, nullptr))
    {
        traceW(L"fault: headObject");

        // エラーは無視
    }

    return true;
}

// EOF