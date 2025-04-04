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

    NTSTATUS ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);

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
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    traceW(L"mObjKey=%s, ctx=%p HANDLE=%p, Offset=%llu, Length=%lu, WriteToEndOfFile=%s, ConstrainedIo=%s",
        ctx->mObjKey.c_str(), ctx, ctx->mFile.handle(), Offset, Length,
        BOOL_CSTRW(WriteToEndOfFile), BOOL_CSTRW(ConstrainedIo));

    NTSTATUS ntstatus = this->prepareLocalFile_simple(CONT_CALLER ctx, Offset, Length);

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

            if (!listObjects(CONT_CALLER argObjKey, &dirInfoList))
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

                if (FA_IS_DIR(dirInfo->FileInfo.FileAttributes))
                {
                    // 削除開始からここまでの間にディレクトリが作成される可能性を考え
                    // 存在したら無視

                    continue;
                }

                const auto fileObjKey{ argObjKey.append(dirInfo->FileNameBuf) };

                Aws::S3::Model::ObjectIdentifier obj;
                obj.SetKey(fileObjKey.keyA());
                delete_objects.AddObjects(obj);

                //
                std::wstring localPath;

                if (!GetCacheFilePath(mCacheDataDir, fileObjKey.str(), &localPath))
                {
                    traceW(L"fault: GetCacheFilePath");
                    return false;
                }

                if (!::DeleteFileW(localPath.c_str()))
                {
                    const auto lerr = ::GetLastError();
                    if (lerr != ERROR_FILE_NOT_FOUND)
                    {
                        traceW(L"fault: DeleteFile");
                        return false;
                    }
                }

                //
                const auto num = deleteObjectCache(CONT_CALLER fileObjKey);
                //traceW(L"cache delete num=%d", num);
            }

            if (delete_objects.GetObjects().empty())
            {
                break;
            }

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

    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());
    const auto outcome = mClient->DeleteObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    // キャッシュ・メモリから削除

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    //traceW(L"cache delete num=%d", num);

    return true;
}

bool AwsS3::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, const wchar_t* sourceFile /* nullable */)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.valid());
    APP_ASSERT(!argObjKey.isBucket());

    traceW(L"argObjKey=%s, sourceFile=%s", argObjKey.c_str(), sourceFile);

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(argObjKey.bucketA());
    request.SetKey(argObjKey.keyA());

    if (sourceFile)
    {
        // ローカル・キャッシュの内容をアップロードする

        APP_ASSERT(argObjKey.meansFile());

        const Aws::String fileName{ WC2MB(sourceFile) };

        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>
        (
            __FUNCTION__,
            fileName.c_str(),
            std::ios_base::in | std::ios_base::binary
        );

        if (!inputData->good())
        {
            traceW(L"fault: inputData->good");
            return false;
        }

        request.SetBody(inputData);
    }

    if (argObjKey.meansFile())
    {
        // ファイルの時のみ

        request.AddMetadata("wincse-creation-time", std::to_string(argFileInfo.CreationTime).c_str());
        //request.AddMetadata("wincse-last-access-time", std::to_string(argFileInfo.LastAccessTime).c_str());
        request.AddMetadata("wincse-last-write-time", std::to_string(argFileInfo.LastWriteTime).c_str());

#if _DEBUG
        request.AddMetadata("wincse-debug-creation-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime).c_str());
        //request.AddMetadata("wincse-debug-last-access-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastAccessTime).c_str());
        request.AddMetadata("wincse-debug-last-write-time", WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime).c_str());
#endif
    }

    const auto outcome = mClient->PutObject(request);

    if (!outcomeIsSuccess(outcome))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    // キャッシュ・メモリから削除
    //
    // 後続の処理で DoGetSecurityByName() が呼ばれるが、上記で作成したディレクトリが
    // キャッシュに反映されていない状態で利用されてしまうことを回避するために
    // 事前に削除しておき、改めてキャッシュを作成させる

    const auto num = deleteObjectCache(CONT_CALLER argObjKey);
    //traceW(L"cache delete num=%d", num);

    return true;
}

// EOF