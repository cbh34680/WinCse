#include "CSDevice.hpp"

using namespace WCSE;


//
// WinFsp の Read() により呼び出され、Offset から Lengh のファイル・データを返却する
// ここでは最初に呼び出されたときに s3 からファイルをダウンロードしてキャッシュとした上で
// そのファイルをオープンし、その後は HANDLE を使いまわす
//
NTSTATUS CSDevice::readObject(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx,
    PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDCtx);
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

NTSTATUS CSDevice::writeObject(CALLER_ARG WCSE::CSDeviceContext* argCSDCtx,
    PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
    PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDCtx);
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

bool CSDevice::deleteObject(CALLER_ARG const ObjectKey& argObjKey)
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

            std::list<std::wstring> delete_objects;

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
                delete_objects.push_back(fileObjKey.key());

                traceW(L"delete_objects.AddObjects fileObjKey=%s", fileObjKey.c_str());

                // ローカルのキャッシュ・ファイルを削除

                const auto localPath{ GetCacheFilePath(mRuntimeEnv->CacheDataDir, fileObjKey.str()) };

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

                const auto num = mQueryObject->deleteCache(CONT_CALLER fileObjKey);
                traceW(L"cache delete num=%d, fileObjKey=%s", num, fileObjKey.c_str());
            }

            if (delete_objects.empty())
            {
                break;
            }

            traceW(L"DeleteObjects bucket=%s size=%zu", argObjKey.bucket().c_str(), delete_objects.size());

            if (!mExecuteApi->DeleteObjects(CONT_CALLER argObjKey.bucket(), delete_objects))
            {
                traceW(L"fault: DeleteObjects");
                return false;
            }
        }
    }

    traceW(L"DeleteObject argObjKey=%s", argObjKey.c_str());

    if (!mExecuteApi->DeleteObject(CONT_CALLER argObjKey))
    {
        traceW(L"fault: DeleteObject");
        return false;
    }

    // キャッシュ・メモリから削除 (ディレクトリ)

    const auto num = mQueryObject->deleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    return true;
}

NTSTATUS CSDevice::setDelete(CALLER_ARG CSDeviceContext* argCSDCtx, BOOLEAN argDeleteFile)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDCtx);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->mObjKey.isObject());

    if (ctx->isDir())
    {
        DirInfoListType dirInfoList;

        if (!this->listObjects(START_CALLER ctx->mObjKey, &dirInfoList))
        {
            traceW(L"fault: listObjects");
            return STATUS_OBJECT_NAME_INVALID;
        }

        decltype(dirInfoList)::const_iterator it;

        switch (mRuntimeEnv->DeleteDirCondition)
        {
            case 1:
            {
                // サブディレクトリがある場合は削除不可

                it = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
                {
                    return wcscmp(dirInfo->FileNameBuf, L".") != 0
                        && wcscmp(dirInfo->FileNameBuf, L"..") != 0
                        && FA_IS_DIRECTORY(dirInfo->FileInfo.FileAttributes);
                });

                break;
            }

            case 2:
            {
                // 空のディレクトリ以外は削除不可

                it = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
                {
                    return wcscmp(dirInfo->FileNameBuf, L".") != 0
                        && wcscmp(dirInfo->FileNameBuf, L"..") != 0;
                });

                break;
            }

            default:
            {
                APP_ASSERT(0);
            }
        }

        if (it != dirInfoList.cend())
        {
            traceW(L"dir not empty");
            return STATUS_CANNOT_DELETE;
            //return STATUS_DIRECTORY_NOT_EMPTY;
        }
    }
    else if (ctx->isFile())
    {
        // キャッシュ・ファイルを削除
        // 
        // remove() などで直接削除するのではなく、削除フラグを設定したファイルを作成し
        // 同時に開かれているファイルが存在しなくなったら、自動的に削除されるようにする
        //
        // このため、キャッシュ・ファイルが存在しない場合は作成しなければ
        // ならないので、他とは異なり OPEN_ALWAYS になっている

        HANDLE Handle = INVALID_HANDLE_VALUE;
        NTSTATUS ntstatus = this->getHandleFromContext(START_CALLER ctx, 0, OPEN_ALWAYS, &Handle);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: getHandleFromContext");
            return ntstatus;
        }

        FILE_DISPOSITION_INFO DispositionInfo{};
        DispositionInfo.DeleteFile = argDeleteFile;

        if (!::SetFileInformationByHandle(Handle,
            FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: SetFileInformationByHandle lerr=%lu", lerr);

            return FspNtStatusFromWin32(lerr);
        }

        traceW(L"success: SetFileInformationByHandle(DeleteFile=%s)", BOOL_CSTRW(argDeleteFile));
    }
    else
    {
        APP_ASSERT(0);
    }

    return STATUS_SUCCESS;
}

bool CSDevice::putObject(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argSourcePath)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(argObjKey.isObject());

    traceW(L"argObjKey=%s, argSourcePath=%s", argObjKey.c_str(), argSourcePath);

    if (!mExecuteApi->PutObject(CONT_CALLER argObjKey, argFileInfo, argSourcePath))
    {
        traceW(L"fault: PutObject");
        return false;
    }

    // キャッシュ・メモリから削除
    //
    // 上記で作成したディレクトリがキャッシュに反映されていない状態で
    // 利用されてしまうことを回避するために事前に削除しておき、改めてキャッシュを作成させる

    const auto num = mQueryObject->deleteCache(CONT_CALLER argObjKey);
    traceW(L"cache delete num=%d, argObjKey=%s", num, argObjKey.c_str());

    // headObject() は必須ではないが、作成直後に属性が参照されることに対応

    if (!this->headObject(CONT_CALLER argObjKey, nullptr))
    {
        traceW(L"fault: headObject");
        return false;
    }

    return true;
}

NTSTATUS CSDevice::renameObject(CALLER_ARG WCSE::CSDeviceContext* ctx, const ObjectKey& argNewObjKey)
{
    NEW_LOG_BLOCK();

    // リモートのオブジェクトを取得

    DirInfoType remoteInfo;

    if (!mExecuteApi->HeadObject(CONT_CALLER ctx->mObjKey, &remoteInfo))
    {
        traceW(L"fault: HeadObject");

        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    const auto localPath{ ctx->getCacheFilePath() };
    const auto newLocalPath{ GetCacheFilePath(ctx->mCacheDataDir, argNewObjKey.str()) };

    if (ctx->isDir())
    {
        // ディレクトリの場合はディレクトリが空の時は OK

        DirInfoListType dirInfoList;

        if (mExecuteApi->ListObjectsV2(CONT_CALLER ctx->mObjKey, false, 2, &dirInfoList))
        {
            const auto it = std::find_if(dirInfoList.cbegin(), dirInfoList.cend(), [](const auto& dirInfo)
            {
                return wcscmp(dirInfo->FileNameBuf, L".") != 0;
            });

            if (it != dirInfoList.cend())
            {
                // "." 以外のファイル名が存在する

                traceW(L"file exists: FileNameBuf=%s", (*it)->FileNameBuf);

                return STATUS_DIRECTORY_NOT_EMPTY;
            }
        }
    }
    else
    {
        // ファイルの場合はローカルとリモートの属性が一致するときは OK

        FSP_FSCTL_FILE_INFO localInfo;
        const auto ntstatus = PathToFileInfo(localPath, &localInfo);
        if (!NT_SUCCESS(ntstatus))
        {
            traceW(L"fault: PathToFileInfo, localPath=%s", localPath.c_str());
            return ntstatus;
        }

        if (localInfo.CreationTime  == remoteInfo->FileInfo.CreationTime &&
            localInfo.LastWriteTime == remoteInfo->FileInfo.LastWriteTime &&
            localInfo.FileSize      == remoteInfo->FileInfo.FileSize)
        {
            // go next
        }
        else
        {
            traceW(L"no match local:remote");
            traceW(L"localPath=%s", localPath.c_str());
            traceW(L"mObjKey=%s", ctx->mObjKey.c_str());
            traceW(L"localInfo:  CreationTime=%llu, LastWriteTime=%llu, FileSize=%llu", localInfo.CreationTime, localInfo.LastWriteTime, localInfo.FileSize);
            traceW(L"remoteInfo: CreationTime=%llu, LastWriteTime=%llu, FileSize=%llu", remoteInfo->FileInfo.CreationTime, remoteInfo->FileInfo.LastWriteTime, remoteInfo->FileInfo.FileSize);

            return STATUS_INVALID_DEVICE_REQUEST;
        }

        // キャッシュ・ファイルのリネーム

        traceW(L"MoveFileExW localPath=%s, newLocalPath=%s", localPath.c_str(), newLocalPath.c_str());

        if (!::MoveFileExW(localPath.c_str(), newLocalPath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: MoveFileExW, lerr=%lu", lerr);

            return FspNtStatusFromWin32(lerr);
        }
    }

    // ローカルにファイルが存在し、リモートと完全に一致する状況なので、リネーム処理を実施

    PCWSTR sourcePath{ ctx->isDir() ? nullptr : newLocalPath.c_str() };

    // 新しい名前でアップロードする

    traceW(L"putObject argNewObjKey=%s, sourcePath=%s", argNewObjKey.c_str(), sourcePath);

    if (!this->putObject(CONT_CALLER argNewObjKey, remoteInfo->FileInfo, sourcePath))
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

    //return STATUS_INVALID_DEVICE_REQUEST;
    return STATUS_SUCCESS;
}

// EOF