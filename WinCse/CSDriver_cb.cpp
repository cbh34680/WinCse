#include "CSDriver.hpp"

#define GET_MIME_TYPE (0)

#if GET_MIME_TYPE
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#endif

using namespace CSELIB;

namespace CSEDRV {

NTSTATUS CSDriver::GetSecurityByName(const std::filesystem::path& argWinPath, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T argSecurityDescriptorSize)
{
    NEW_LOG_BLOCK();

    // OpenDirEntry から取得

    traceW(L"argWinPath=%s", argWinPath.c_str());

    auto dirEntry{ mOpenDirEntry.get(argWinPath) };
    if (!dirEntry)
    {
        if (mDevice->shouldIgnoreWinPath(argWinPath))
        {
            // "desktop.ini" などは無視させる

            traceW(L"ignore argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        // ファイル名からリモートの情報を取得

        dirEntry = this->getDirEntryByWinPath(START_CALLER argWinPath);
        if (!dirEntry)
        {
            traceW(L"fault: getDirEntryByWinPath, argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }
    }

    HANDLE hFile = INVALID_HANDLE_VALUE;

    switch (dirEntry->mFileType)
    {
        case FileTypeEnum::Directory:
        {
            traceW(L"argWinPath=%s [DIRECTORY]", argWinPath.c_str());

            [[fallthrough]];
        }
        case FileTypeEnum::Root:
        case FileTypeEnum::Bucket:
        {
            hFile = mRuntimeEnv->DirSecurityRef.handle();
            break;
        }

        case FileTypeEnum::File:
        {
            traceW(L"argWinPath=%s [FILE]", argWinPath.c_str());

            hFile = mRuntimeEnv->FileSecurityRef.handle();
            break;
        }

        default:
        {
            APP_ASSERT(0);
            break;
        }
    }

    const auto ntstatus = HandleToSecurityInfo(hFile, argSecurityDescriptor, argSecurityDescriptorSize);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: HandleToSecurityInfo argWinPath=%s", argWinPath.c_str());
        return ntstatus;
    }

    if (pFileAttributes)
    {
        *pFileAttributes = dirEntry->mFileInfo.FileAttributes;
    }

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::Open(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argWinPath=%s argCreateOptions=%u argGrantedAccess=%u", argWinPath.c_str(), argCreateOptions, argGrantedAccess);

    // 既にオープンしているものがあれば、それを採用

    bool addRefCount = false;

    auto dirEntry{ mOpenDirEntry.get(argWinPath) };
    if (dirEntry)
    {
        // 最初に acquire を呼び出すと関数中でエラーが発生したときに release する
        // 必要が発生するので、ここでは get して最後に acquire を呼び出す

        addRefCount = true;
    }
    else
    {
        if (mDevice->shouldIgnoreWinPath(argWinPath))
        {
            // "desktop.ini" などは無視させる
            // --> GetSecurityByName で拒否しているのでここは通過しないはず

            traceW(L"ignore argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        // オープンしているものがなければ、dirEntry を取得

        dirEntry = this->getDirEntryByWinPath(START_CALLER argWinPath);
        if (!dirEntry)
        {
            errorW(L"fault: getDirEntryByWinPath argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }
    }

    traceW(L"addRefCount=%s dirEntry=%s", BOOL_CSTRW(addRefCount), dirEntry->str().c_str());

    // コンテクストの作成

    std::unique_ptr<FileContext> ctx;

    switch (dirEntry->mFileType)
    {
        case FileTypeEnum::Root:
        case FileTypeEnum::Bucket:
        {
            // 参照用のハンドルを設定してコンテクストを生成

            ctx = std::make_unique<RefFileContext>(argWinPath, dirEntry, mRuntimeEnv->DirSecurityRef.handle());

            break;
        }

        case FileTypeEnum::Directory:
        {
            // 参照用のハンドルを設定してコンテクストを生成

            ctx = std::make_unique<RefFileContext>(argWinPath, dirEntry, mRuntimeEnv->DirSecurityRef.handle());

            break;
        }

        case FileTypeEnum::File:
        {
            // キャッシュファイルの属性(サイズ以外) をリモートと同期する

            std::filesystem::path cacheFilePath;

            if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, argWinPath, &cacheFilePath))
            {
                errorW(L"fault: resolveCacheFilePath argWinPath=%s", argWinPath.c_str());
                return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
            }

            const auto ntstatus = syncAttributes(dirEntry, cacheFilePath);
            if (!NT_SUCCESS(ntstatus))
            {
                errorW(L"fault: syncRemoteAttributes dirEntry=%s", dirEntry->str().c_str());
                return ntstatus;
            }

            // キャッシュファイルを開きコンテクストに保存

            ULONG CreateFlags = 0;

            if (argCreateOptions & FILE_DELETE_ON_CLOSE)
            {
                CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
            }

            FileHandle file = ::CreateFileW(
                cacheFilePath.c_str(),
                argGrantedAccess,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                OPEN_EXISTING,
                CreateFlags,
                NULL);

            if (file.invalid())
            {
                const auto lerr = ::GetLastError();

                errorW(L"fault: CreateFile lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
                return FspNtStatusFromWin32(lerr);
            }

            ctx = std::make_unique<OpenFileContext>(argWinPath, dirEntry, std::move(file));

            break;
        }
    }

    traceW(L"ctx=%s", ctx->str().c_str());

    if (addRefCount)
    {
        // 参照カウントを増やす

        if (!mOpenDirEntry.acquire(argWinPath))
        {
            errorW(L"fault: acquire argWinPath=%s", argWinPath.c_str());
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        // 取得した dirEntry をオープン中として登録

        if (!mOpenDirEntry.addAndAcquire(argWinPath, dirEntry))
        {
            errorW(L"fault: addAndAcquire argWinPath=%s", argWinPath.c_str());
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // WinFsp に返却

    *pFileInfo = dirEntry->mFileInfo;
    *pFileContext = ctx.release();

	return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Create(const std::filesystem::path& argWinPath, UINT32 argCreateOptions, UINT32 argGrantedAccess, UINT32 argFileAttributes, PSECURITY_DESCRIPTOR argSecurityDescriptor, UINT64 argAllocationSize, FileContext** pFileContext, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argWinPath=%s argCreateOptions=%u argCreateOptions=%u argFileAttributes=%u argAllocationSize=%llu", argWinPath.c_str(), argCreateOptions, argCreateOptions, argFileAttributes, argAllocationSize);
    traceW(L"argFileAttributes=%s", FileAttributesToStringW(argFileAttributes).c_str());

    if (mDevice->shouldIgnoreWinPath(argWinPath))
    {
        traceW(L"ignore argWinPath=%s", argWinPath.c_str());
        return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
    }

    const bool isDir = argCreateOptions & FILE_DIRECTORY_FILE;

    // 引数のファイル名が既に存在しているかを確認

    std::optional<ObjectKey> optObjKey;

    const auto ntstatus = this->canCreateObject(START_CALLER argWinPath, isDir, &optObjKey);
    if (!NT_SUCCESS(ntstatus))
    {
        if (ntstatus != STATUS_OBJECT_NAME_COLLISION)
        {
            errorW(L"fault: canCreateObject argWinPath=%s ntstatus=%ld", argWinPath.c_str(), ntstatus);
        }

        return ntstatus;
    }

    // ディレクトリ・エントリの作成

    const auto objKey{ *optObjKey };

    std::wstring filename;
    if (!SplitObjectKey(objKey.str(), nullptr, &filename))
    {
        errorW(L"fault: SplitObjectKey objKey=%s", objKey.c_str());
        return STATUS_OBJECT_NAME_INVALID;
    }

    const auto fileTime = GetCurrentWinFileTime100ns();

    const auto dirEntry = isDir
        ? DirectoryEntry::makeDirectoryEntry(filename, fileTime)
        : DirectoryEntry::makeFileEntry(filename, 0, fileTime);

    traceW(L"dirEntry=%s", dirEntry->str().c_str());

    // コンテクストの作成

    const auto dirInfoPtr{ dirEntry->makeDirInfo() };

    std::unique_ptr<FileContext> ctx;

    if (isDir)
    {
        // リモートにディレクトリを作成

        if (!mDevice->putObject(START_CALLER objKey, dirInfoPtr->FileInfo, nullptr))
        {
            errorW(L"fault: putObject objKey=%s", objKey.c_str());

            return STATUS_INVALID_DEVICE_REQUEST;
        }

        ctx = std::make_unique<RefFileContext>(argWinPath, dirEntry, mRuntimeEnv->DirSecurityRef.handle());
    }
    else
    {
        // キャッシュファイルの作成

        std::filesystem::path cacheFilePath;

        if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, argWinPath, &cacheFilePath))
        {
            errorW(L"fault: resolveCacheFilePath argWinPath=%s", argWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
        }

        ULONG CreateFlags = 0;

        if (argCreateOptions & FILE_DELETE_ON_CLOSE)
        {
            CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;
        }

        argFileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;

        if (0 == argFileAttributes)
        {
            argFileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

        FileHandle file = ::CreateFileW(
            cacheFilePath.c_str(),
            argGrantedAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            CREATE_ALWAYS,
            CreateFlags | argFileAttributes,
            NULL);

        if (file.invalid())
        {
            errorW(L"fault: CreateFileW cacheFilePath=%s", cacheFilePath.c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        ctx = std::make_unique<OpenFileContext>(argWinPath, dirEntry, std::move(file));
    }

    traceW(L"ctx=%s", ctx->str().c_str());

    // 取得した dirEntry をオープン中として登録

    if (!mOpenDirEntry.addAndAcquire(argWinPath, dirEntry))
    {
        errorW(L"fault: addAndAcquire argWinPath=%s", argWinPath.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // WinFsp に返却

    *pFileInfo = dirEntry->mFileInfo;
    *pFileContext = ctx.release();

	return STATUS_SUCCESS;
}

void CSDriver::UploadWhenClosing(CALLER_ARG FileContext* ctx)
{
    NEW_LOG_BLOCK();

    // 内容に変更があり、リモートへの反映が必要な状態

    const auto& dirEntry{ ctx->getDirEntry() };
    const auto objKey{ ctx->getObjectKey() };

    switch (dirEntry->mFileType)
    {
        case FileTypeEnum::File:
        {
            //::SwitchToThread();

            // キャッシュファイルのパスを取得

            std::filesystem::path cacheFilePath;

            if (!GetFileNameFromHandle(ctx->getHandle(), &cacheFilePath))
            {
                errorW(L"fault: GetFileNameFromHandle ctx=%s", ctx->str().c_str());
                break;
            }

            // 未ダウンロード部分を取得する

            auto ntstatus = this->syncContent(CONT_CALLER ctx, 0, (FILEIO_LENGTH_T)dirEntry->mFileInfo.FileSize);
            if (!NT_SUCCESS(ntstatus))
            {
                errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
                break;
            }

            // アップロードの実行

            if (!mDevice->putObject(START_CALLER objKey, dirEntry->mFileInfo, cacheFilePath.c_str()))
            {
                errorW(L"fault: putObject objKey=%s", objKey.c_str());
                break;
            }

            traceW(L"success: putObject objKey=%s", objKey.c_str());

            // キャッシュの更新
            // robocopy 対策

            mDevice->headObject(START_CALLER objKey, nullptr);

            switch (mRuntimeEnv->DeleteAfterUpload)
            {
                case 1:
                {
                    // アップロード後にファイルを削除

                    if (::DeleteFileW(cacheFilePath.c_str()))
                    {
                        traceW(L"success: DeleteFileW cacheFilePath=%s", cacheFilePath.c_str());
                    }
                    {
                        const auto lerr = ::GetLastError();
                        errorW(L"fault: DeleteFileW lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
                    }

                    break;
                }

                case 2:
                {
                    // アップロード後にファイルを切り詰める (あまり意味はないかな、、)

                    if (TruncateFile(cacheFilePath.c_str()))
                    {
                        traceW(L"success: TruncateFile cacheFilePath=%s", cacheFilePath.c_str());
                    }
                    else
                    {
                        const auto lerr = ::GetLastError();
                        errorW(L"fault: TruncateFile lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
                    }

                    break;
                }
            }

            break;
        }
    }
}

VOID CSDriver::Close(FileContext* ctx)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    if (ctx->getHandle() == INVALID_HANDLE_VALUE)
    {
        // SetDelete で削除フラグが立ち、Cleanup で閉じられている

        traceW(L"already closed");
    }
    else if (ctx->mFlags & FCTX_FLAGS_MODIFY)
    {
        this->UploadWhenClosing(START_CALLER ctx);
    }

    // オープン中の情報から削除

    const bool b = mOpenDirEntry.release(ctx->getWinPath());
    if (!b)
    {
        traceW(L"fault: already released");
    }

    // Open() で生成したメモリを解放

    delete ctx;
}

#pragma warning(suppress: 4100)
VOID CSDriver::Cleanup(FileContext* ctx, PCWSTR argWinPath, ULONG argFlags)
{
    NEW_LOG_BLOCK();

    traceW(L"argWinPath=%s argFlags=%lu ctx=%s", argWinPath, argFlags, ctx->str().c_str());

    if (argFlags & FspCleanupDelete)
    {
        const auto& refWinPath{ ctx->getWinPath() };

        switch (ctx->getDirEntry()->mFileType)
        {
            case FileTypeEnum::Directory:
            {
                // あまり意味はないが一応 :-)

                traceW(L"closeHandle");
                ctx->closeHandle();

                break;
            }

            case FileTypeEnum::File:
            {
                std::filesystem::path cacheFilePath;
                if (resolveCacheFilePath(mRuntimeEnv->CacheDataDir, refWinPath, &cacheFilePath))
                {
                    const auto faBefore = ::GetFileAttributesW(cacheFilePath.c_str());

                    traceW(L"closeHandle");
                    ctx->closeHandle();

                    const auto faAfter = ::GetFileAttributesW(cacheFilePath.c_str());

                    if (faBefore != INVALID_FILE_ATTRIBUTES && faAfter == INVALID_FILE_ATTRIBUTES)
                    {
                        // ハンドルをクローズしたことで属性が変化した
                        // --> FILE_FLAG_DELETE_ON_CLOSE の影響によりキャッシュファイルが削除されたとき

                        // リモートの削除

                        if (!mDevice->deleteObject(START_CALLER ctx->getObjectKey()))
                        {
                            errorW(L"fault: deleteObject");
                        }
                    }
                }
                else
                {
                    errorW(L"fault: resolveCacheFilePath refWinPath=%s", refWinPath.c_str());

                    traceW(L"closeHandle");
                    ctx->closeHandle();
                }

                break;
            }
        }

        // オープン中の情報から削除

        const bool b = mOpenDirEntry.release(refWinPath);
        if (!b)
        {
            traceW(L"fault: mOpenDirEntry.release");
        }
    }
}

NTSTATUS CSDriver::Flush(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    // MS EXCEL を新規作成すると通過する

    traceW(L"ctx=%s", ctx->str().c_str());

    HANDLE Handle = ctx->getWritableHandle();

    /* we do not flush the whole volume, so just return SUCCESS */
    //if (0 == Handle)
    //    return STATUS_SUCCESS;

    if (!::FlushFileBuffers(Handle))
    {
        errorW(L"fault: FlushFileBuffers ctx=%s", ctx->str().c_str());
        return FspNtStatusFromWin32(::GetLastError());
    }

    // ファイルサイズに変更はないので、Write と同じ扱いで良いはず (true)

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, true);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

NTSTATUS CSDriver::GetFileInfo(FileContext* ctx, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    *pFileInfo = ctx->getDirEntry()->mFileInfo;

	return STATUS_SUCCESS;
}

NTSTATUS CSDriver::GetSecurity(FileContext* ctx, PSECURITY_DESCRIPTOR argSecurityDescriptor, PSIZE_T pSecurityDescriptorSize)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    DWORD SecurityDescriptorSizeNeeded = 0;

    if (!::GetKernelObjectSecurity(ctx->getHandle(),
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        argSecurityDescriptor, (DWORD)*pSecurityDescriptorSize, &SecurityDescriptorSizeNeeded))
    {
        errorW(L"fault: GetKernelObjectSecurity ctx=%s", ctx->str().c_str());

        *pSecurityDescriptorSize = SecurityDescriptorSizeNeeded;
        return FspNtStatusFromWin32(::GetLastError());
    }

    *pSecurityDescriptorSize = SecurityDescriptorSizeNeeded;

    return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Overwrite(FileContext* ctx, UINT32 argFileAttributes, BOOLEAN argReplaceFileAttributes, UINT64 argAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    HANDLE Handle = ctx->getWritableHandle();

    FILE_BASIC_INFO BasicInfo{};
    FILE_ALLOCATION_INFO AllocationInfo{};
    FILE_ATTRIBUTE_TAG_INFO AttributeTagInfo{};

    if (argReplaceFileAttributes)
    {
        if (0 == argFileAttributes)
        {
            argFileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

        BasicInfo.FileAttributes = argFileAttributes;

        if (!::SetFileInformationByHandle(Handle, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
        {
            errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }
    }
    else if (0 != argFileAttributes)
    {
        if (!::GetFileInformationByHandleEx(Handle, FileAttributeTagInfo, &AttributeTagInfo, sizeof AttributeTagInfo))
        {
            errorW(L"fault: GetFileInformationByHandleEx ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        BasicInfo.FileAttributes = argFileAttributes | AttributeTagInfo.FileAttributes;

        if (BasicInfo.FileAttributes ^ argFileAttributes)
        {
            if (!::SetFileInformationByHandle(Handle, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
            {
                errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
                return FspNtStatusFromWin32(::GetLastError());
            }
        }
    }

    if (!::SetFileInformationByHandle(Handle, FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
    {
        errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
        return FspNtStatusFromWin32(::GetLastError());
    }

    // キャッシュファイルが切り詰められているので、ファイル情報を優先(false)する

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, false);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

NTSTATUS CSDriver::Read(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, PULONG argBytesTransferred)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    // リモートの内容と部分同期 (argOffset + argLengh の範囲)

    const auto ntstatus = this->syncContent(START_CALLER ctx, (FILEIO_OFFSET_T)argOffset, argLength);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    HANDLE Handle = ctx->getWritableHandle();

    OVERLAPPED Overlapped{};

    Overlapped.Offset     = static_cast<DWORD>(argOffset);
    Overlapped.OffsetHigh = static_cast<DWORD>(argOffset >> 32);

    traceW(L"ReadFile argOffset=%llu argLength=%lu", argOffset, argLength);

    if (!::ReadFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
    {
        const auto lerr = ::GetLastError();

        if (lerr == ERROR_HANDLE_EOF)
        {
            traceW(L"EOF");
        }
        else
        {
            errorW(L"fault: ReadFile ctx=%s", ctx->str().c_str());
        }

        return FspNtStatusFromWin32(lerr);
    }

    traceW(L"success: ReadFile argBytesTransferred=%lu", *argBytesTransferred);

#if GET_MIME_TYPE
    if (argOffset == 0 && *argBytesTransferred)
    {
        // ファイルの先頭部分から Content-Type を算出してプロパティに設定

        auto& props{ ctx->getDirEntry()->mUserProperties };

        if (props.find(L"wincse-content-type") == props.cend())
        {
            LPWSTR mimeType = nullptr;

            HRESULT hr = ::FindMimeFromData(nullptr, nullptr, argBuffer, *argBytesTransferred, nullptr, 0, &mimeType, 0);
            if (SUCCEEDED(hr))
            {
                props.insert({ L"wincse-content-type", mimeType });

                ::CoTaskMemFree(mimeType);
            }
        }
    }
#endif

    return STATUS_SUCCESS;
}

//
// 他のメソッドと異なり基底クラスの RelayReadDirectory() では排他制御を行わず、ここに記述している。
// これは、排他制御の必要な対象が複数存在するためである。
// 
//  1) コンテクストに設定された mFileName での排他制御            ... 他の関数と同義
//  2) リストした個々のオブジェクトごとの排他制御
// 
// これにより、他のメソッドが利用しているオブジェクトと同期できるはず
//
NTSTATUS CSDriver::ReadDirectory(FileContext* ctx, PCWSTR argPattern, PWSTR argMarker, PVOID argBuffer, ULONG argBufferLength, PULONG argBytesTransferred)
{
    NEW_LOG_BLOCK();

    traceW(L"argPattern=%s argMarker=%s ctx=%s", argPattern, argMarker, ctx->str().c_str());

    // オープン中の情報のうち、親ディレクトリが一致するものを取得

    const auto& refWinPath{ ctx->getWinPath() };

    const auto& is_same_dir = [&refWinPath](const OpenDirEntry::copy_type::value_type& value)
    {
        if (refWinPath == L"\\")
        {
            return false;
        }

        if (refWinPath == value.first)
        {
            return false;
        }

        const auto parentPath{ value.first.parent_path() };

        return refWinPath == parentPath;
    };

    const auto openDirEntry{ mOpenDirEntry.copy_if(is_same_dir) };

    std::optional<std::wregex> reWildcard;

    if (argPattern)
    {
        // 引数のパターンを正規表現に変換

        reWildcard = WildcardToRegexW(argPattern);
    }

    // ディレクトリの中の一覧取得

    DirEntryListType dirEntryList;

    // 1) コンテクストの mFileName による排他制御

    {
        UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, ctx->getWinPath() };
        {
            const auto safeShare{ unsafeShare.lock() };

            bool isBucket = false;

            switch (ctx->getDirEntry()->mFileType)
            {
                case FileTypeEnum::Root:
                {
                    // "\" へのアクセスはバケット一覧を提供

                    if (!mDevice->listBuckets(START_CALLER &dirEntryList))
                    {
                        errorW(L"fault: listBuckets ctx=%s", ctx->str().c_str());

                        return STATUS_OBJECT_NAME_INVALID;
                    }

                    break;
                }

                case FileTypeEnum::Bucket:
                {
                    isBucket = true;

                    [[fallthrough]];
                }
                case FileTypeEnum::Directory:
                {
                    // "\bucket" または "\bucket\key"

                    const auto objKey{ ctx->getObjectKey() };

                    if (!mDevice->listDisplayObjects(START_CALLER objKey, &dirEntryList))
                    {
                        errorW(L"fault: listDisplayObjects objKey=%s", objKey.c_str());

                        return STATUS_OBJECT_NAME_INVALID;
                    }

                    break;
                }

                default:
                {
                    APP_ASSERT(0);

                    break;
                }
            }
        }
    }

    if (dirEntryList.empty())
    {
        traceW(L"empty buckets");
        return STATUS_SUCCESS;
    }

    // 取得したものを WinFsp に転送する

    const auto dirInfoAllocSize = sizeof(FSP_FSCTL_DIR_INFO) + (MAX_PATH * sizeof(WCHAR));
    std::vector<BYTE> dirInfoBuf(dirInfoAllocSize);
    FSP_FSCTL_DIR_INFO* dirInfo = (FSP_FSCTL_DIR_INFO*)dirInfoBuf.data();

    NTSTATUS fspNtstatus = STATUS_UNSUCCESSFUL;

    if (FspFileSystemAcquireDirectoryBuffer(&ctx->mDirBuffer, 0 == argMarker, &fspNtstatus))
    {
        std::set<std::filesystem::path> already;

        for (const auto& dirEntry: dirEntryList)
        {
            const std::wstring& fileNameBuf{ dirEntry->getFileNameBuf() };

            if (reWildcard)
            {
                if (!std::regex_match(fileNameBuf, *reWildcard))
                {
                    traceW(L"reWildcard no match fileNameBuf=%s", fileNameBuf.c_str());
                    continue;
                }
            }

            const auto winPath{ refWinPath / fileNameBuf };

            if (mDevice->shouldIgnoreWinPath(winPath))
            {
                traceW(L"ignore winPath=%s", winPath.c_str());
                continue;
            }

            traceW(L"winPath=%s", winPath.c_str());

            // 2) リストした個々のオブジェクトごとの排他制御

            UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, winPath };
            {
                // ファイル名で排他制御し、FSP_FSCTL_DIR_INFO* が変更されないようにする

                const auto safeShare{ unsafeShare.lock() };

                // オープン中のディレクトリエントリがあればそれを、そうでなければリストされたオブジェクト
                // のディレクトリエントリを選択して FSP_FSCTL_DIR_INFO を生成

                const auto it{ openDirEntry.find(winPath) };

                if (it == openDirEntry.cend())
                {
                    memset(dirInfo, 0, dirInfoAllocSize);
                    dirEntry->getDirInfo(dirInfo);
                }
                else
                {
                    memset(dirInfo, 0, dirInfoAllocSize);
                    it->second->getDirInfo(dirInfo);
                }
                APP_ASSERT(dirInfo->FileInfo.FileAttributes);

                // readonly 属性を反映

                this->applyDefaultFileAttributes(&dirInfo->FileInfo);

                // WinFsp に送信

                if (!FspFileSystemFillDirectoryBuffer(&ctx->mDirBuffer, dirInfo, &fspNtstatus))
                {
                    errorW(L"fault: FspFileSystemFillDirectoryBuffer ctx=%s", ctx->str().c_str());
                    break;
                }
            }

            already.insert(winPath);
        }

        for (const auto& it: openDirEntry)
        {
            if (already.find(it.first) != already.cend())
            {
                // 上記で登録済

                traceW(L"already: it.first=%s", it.first.c_str());
                continue;
            }

            traceW(L"new file it.first=%s", it.first.c_str());

            UnprotectedShare<FileNameGuard> unsafeShare{ &mFileNameGuard, it.first };
            {
                // ファイル名で排他制御し、FSP_FSCTL_DIR_INFO* が変更されないようにする

                const auto safeShare{ unsafeShare.lock() };

                // 新規作成されたファイル&ディレクトリ

                memset(dirInfo, 0, dirInfoAllocSize);
                it.second->getDirInfo(dirInfo);

                if (!FspFileSystemFillDirectoryBuffer(&ctx->mDirBuffer, dirInfo, &fspNtstatus))
                {
                    errorW(L"fault: FspFileSystemFillDirectoryBuffer ctx=%s", ctx->str().c_str());
                    break;
                }
            }
        }

        FspFileSystemReleaseDirectoryBuffer(&ctx->mDirBuffer);
    }
    else
    {
        traceW(L"fault: FspFileSystemAcquireDirectoryBuffer argMarker=%s", argMarker);
    }

    if (!NT_SUCCESS(fspNtstatus))
    {
        errorW(L"fault: FspFilesystem*** ctx=%s", ctx->str().c_str());
        return fspNtstatus;
    }

    FspFileSystemReadDirectoryBuffer(&ctx->mDirBuffer, argMarker, argBuffer, argBufferLength, argBytesTransferred);

    return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Rename(FileContext* ctx, const std::filesystem::path& argSrcWinPath, const std::filesystem::path& argDstWinPath, BOOLEAN argReplaceIfExists)
{
    NEW_LOG_BLOCK();

    traceW(L"argSrcWinPath=%s argDstWinPath=%s argReplaceIfExists=%s ctx=%s", argSrcWinPath.c_str(), argDstWinPath.c_str(), BOOL_CSTRW(argReplaceIfExists), ctx->str().c_str());

    if (mDevice->shouldIgnoreWinPath(argDstWinPath))
    {
        traceW(L"ignore argDstWinPath=%s", argDstWinPath.c_str());
        return STATUS_ACCESS_DENIED;
    }

    const bool isDir = FA_IS_DIR(ctx->getDirEntry()->mFileInfo.FileAttributes);

    std::optional<ObjectKey> optDstObjKey;

    // リネーム先の名前が存在するか確認

    auto ntstatus = this->canCreateObject(START_CALLER argDstWinPath, isDir, &optDstObjKey);
    if (!NT_SUCCESS(ntstatus))
    {
        if (ntstatus == STATUS_OBJECT_NAME_COLLISION)
        {
            traceW(L"already exists: argDstWinPath=%s", argDstWinPath.c_str());
        }
        else
        {
            errorW(L"fault: canCreateObject argDstWinPath=%s", argDstWinPath.c_str());
        }

        return ntstatus;
    }

    const auto srcObjKey{ ctx->getObjectKey() };

    traceW(L"srcObjKey=%s", srcObjKey.c_str());

    if (isDir)
    {
        // ディレクトリの場合は空の時のみリネーム可

        DirEntryListType dirEntryList;

        if (!mDevice->listObjects(START_CALLER srcObjKey, &dirEntryList))
        {
            errorW(L"fault: listObjects srcObjKey=%s", srcObjKey.c_str());
            return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
        }

        if (!dirEntryList.empty())
        {
            traceW(L"not empty srcObjKey=%s", srcObjKey.c_str());
            return STATUS_DIRECTORY_NOT_EMPTY;
        }
    }

    const auto dstObjKey{ *optDstObjKey };

    traceW(L"dstObjKey=%s", dstObjKey.c_str());

    const auto& srcFileInfo{ ctx->getDirEntry()->mFileInfo };

    // リネーム先のディレクトリエントリを作成

    std::wstring filename;
    if (!SplitObjectKey(dstObjKey.str(), nullptr, &filename))
    {
        errorW(L"fault: SplitObjectKey dstObjKey=%s", dstObjKey.c_str());
        return STATUS_OBJECT_NAME_INVALID;
    }

    const auto dstDirEntry = isDir
        ? DirectoryEntry::makeDirectoryEntry(filename,                       srcFileInfo.CreationTime, srcFileInfo.LastAccessTime, srcFileInfo.LastWriteTime, srcFileInfo.ChangeTime)
        : DirectoryEntry::makeFileEntry(     filename, srcFileInfo.FileSize, srcFileInfo.CreationTime, srcFileInfo.LastAccessTime, srcFileInfo.LastWriteTime, srcFileInfo.ChangeTime);

    traceW(L"dstDirEntry=%s", dstDirEntry->str().c_str());

    const auto dstDirInfoPtr{ dstDirEntry->makeDirInfo() };

    // リネーム元をオープン中の情報から削除

    const bool b = mOpenDirEntry.release(ctx->getWinPath());
    if (!b)
    {
        traceW(L"fault: already released");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // リモートのオブジェクトをコピー

    if (!mDevice->copyObject(START_CALLER srcObjKey, dstObjKey))
    {
        errorW(L"fault: copyObject copyObject=%s dstObjKey=%s", srcObjKey.c_str(), dstObjKey.c_str());
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    if (!isDir)
    {
        // キャッシュ・ファイル名を変更

        // リネーム元のキャッシュ・ファイル名を取得

        std::filesystem::path orgCacheFilePath;

        if (!GetFileNameFromHandle(ctx->getHandle(), &orgCacheFilePath))
        {
            errorW(L"fault: GetFileNameFromHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        // リネーム先のキャッシュ・ファイル名を作成

        std::filesystem::path dstCacheFilePath;

        if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, argDstWinPath, &dstCacheFilePath))
        {
            errorW(L"fault: resolveCacheFilePath argDstWinPath=%s", argDstWinPath.c_str());
            return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
        }

        // キャッシュ・ファイルのリネーム

        traceW(L"MoveFileExW orgCacheFilePath=%s, dstCacheFilePath=%s", orgCacheFilePath.c_str(), dstCacheFilePath.c_str());

        if (!::MoveFileExW(orgCacheFilePath.c_str(), dstCacheFilePath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const auto lerr = ::GetLastError();
            errorW(L"fault: MoveFileExW lerr=%lu orgCacheFilePath=%s, dstCacheFilePath=%s", lerr, orgCacheFilePath.c_str(), dstCacheFilePath.c_str());

            return FspNtStatusFromWin32(lerr);
        }
    }

    // リモートのリネーム元を削除

    traceW(L"deleteObject srcObjKey=%s", srcObjKey.c_str());

    if (!mDevice->deleteObject(START_CALLER srcObjKey))
    {
        errorW(L"fault: deleteObject srcObjKey=%s", srcObjKey.c_str());
        return FspNtStatusFromWin32(ERROR_IO_DEVICE);
    }

    // コンテクスト情報の入れ替え

    ctx->rename(argDstWinPath, dstDirEntry);

    // リネーム先をオープン中として登録

    if (!mOpenDirEntry.addAndAcquire(argDstWinPath, dstDirEntry))
    {
        errorW(L"fault: addAndAcquire argDstWinPath=%s", argDstWinPath.c_str());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

	return STATUS_SUCCESS;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::SetBasicInfo(FileContext* ctx, UINT32 argFileAttributes, UINT64 argCreationTime, UINT64 argLastAccessTime, UINT64 argLastWriteTime, UINT64 argChangeTime, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argFileAttributes=%u argCreationTime=%llu argLastAccessTime=%llu argLastWriteTime=%llu argChangeTime=%llu ctx=%s",
        argFileAttributes, argCreationTime, argLastAccessTime, argLastWriteTime, argChangeTime, ctx->str().c_str());

    switch (ctx->getDirEntry()->mFileType)
    {
        case FileTypeEnum::Directory:
        {
            *pFileInfo = ctx->getDirEntry()->mFileInfo;

            return STATUS_SUCCESS;

            break;
        }

        case FileTypeEnum::File:
        {
            HANDLE Handle = ctx->getWritableHandle();

            FILE_BASIC_INFO BasicInfo{};

            if (INVALID_FILE_ATTRIBUTES == argFileAttributes)
            {
                argFileAttributes = 0;
            }
            else if (0 == argFileAttributes)
            {
                argFileAttributes = FILE_ATTRIBUTE_NORMAL;
            }

            BasicInfo.FileAttributes = argFileAttributes;
            BasicInfo.CreationTime.QuadPart = argCreationTime;
            BasicInfo.LastAccessTime.QuadPart = argLastAccessTime;
            BasicInfo.LastWriteTime.QuadPart = argLastWriteTime;
            //BasicInfo.ChangeTime = argChangeTime;

            if (!::SetFileInformationByHandle(Handle, FileBasicInfo, &BasicInfo, sizeof BasicInfo))
            {
                errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
                return FspNtStatusFromWin32(::GetLastError());
            }

            // ファイルサイズに変更はないので Write と同じ扱い(true)

            return this->updateFileInfo(START_CALLER ctx, pFileInfo, true);
            //return GetFileInfoInternal(Handle, pFileInfo);

            break;
        }
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS CSDriver::SetFileSize(FileContext* ctx, UINT64 argNewSize, BOOLEAN argSetAllocationSize, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"argNewSize=%llu argSetAllocationSize=%s ctx=%s", argNewSize, BOOL_CSTRW(argSetAllocationSize), ctx->str().c_str());

    // 未ダウンロード部分を取得する

    auto ntstatus = this->syncContent(START_CALLER ctx, 0, (FILEIO_LENGTH_T)argNewSize);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    HANDLE Handle = ctx->getWritableHandle();

    FILE_ALLOCATION_INFO AllocationInfo{};
    FILE_END_OF_FILE_INFO EndOfFileInfo{};

    if (argSetAllocationSize)
    {
        /*
        * This file system does not maintain AllocationSize, although NTFS clearly can.
        * However it must always be FileSize <= AllocationSize and NTFS will make sure
        * to truncate the FileSize if it sees an AllocationSize < FileSize.
        *
        * If OTOH a very large AllocationSize is passed, the call below will increase
        * the AllocationSize of the underlying file, although our file system does not
        * expose this fact. This AllocationSize is only temporary as NTFS will reset
        * the AllocationSize of the underlying file when it is closed.
        */

        AllocationInfo.AllocationSize.QuadPart = argNewSize;

        if (!::SetFileInformationByHandle(Handle, FileAllocationInfo, &AllocationInfo, sizeof AllocationInfo))
        {
            errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }
    }
    else
    {
        EndOfFileInfo.EndOfFile.QuadPart = argNewSize;

        if (!::SetFileInformationByHandle(Handle, FileEndOfFileInfo, &EndOfFileInfo, sizeof EndOfFileInfo))
        {
            errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }
    }

    // ファイルサイズの変更に必要な部分は同期されており、ローカルのファイルサイズを優先(false)する

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, false);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::SetSecurity(FileContext* argFileContext, SECURITY_INFORMATION argSecurityInformation, PSECURITY_DESCRIPTOR argModificationDescriptor)
{
	return STATUS_INVALID_DEVICE_REQUEST;
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::Write(FileContext* ctx, PVOID argBuffer, UINT64 argOffset, ULONG argLength, BOOLEAN argWriteToEndOfFile, BOOLEAN argConstrainedIo, PULONG argBytesTransferred, FSP_FSCTL_FILE_INFO* pFileInfo)
{
    NEW_LOG_BLOCK();

    traceW(L"ctx=%s", ctx->str().c_str());

    // リモートの内容と部分同期 (argOffset + argLengh の範囲)

    auto ntstatus = this->syncContent(START_CALLER ctx, (FILEIO_OFFSET_T)argOffset, (FILEIO_LENGTH_T)argLength);
    if (!NT_SUCCESS(ntstatus))
    {
        errorW(L"fault: syncContent ctx=%s", ctx->str().c_str());
        return ntstatus;
    }

    HANDLE Handle = ctx->getWritableHandle();
    LARGE_INTEGER FileSize{};
    OVERLAPPED Overlapped{};

    if (argConstrainedIo)
    {
        if (!::GetFileSizeEx(Handle, &FileSize))
        {
            errorW(L"fault: GetFileSizeEx ctx=%s", ctx->str().c_str());
            return FspNtStatusFromWin32(::GetLastError());
        }

        if (static_cast<INT64>(argOffset) >= FileSize.QuadPart)
        {
            return STATUS_SUCCESS;
        }

        if (static_cast<INT64>(argOffset) + argLength > FileSize.QuadPart)
        {
            argLength = static_cast<ULONG>(FileSize.QuadPart - static_cast<INT64>(argOffset));
        }
    }

    Overlapped.Offset = static_cast<DWORD>(argOffset);
    Overlapped.OffsetHigh = static_cast<DWORD>(argOffset >> 32);

    traceW(L"WriteFile argOffset=%llu argLength=%lu", argOffset, argLength);

    if (!::WriteFile(Handle, argBuffer, argLength, argBytesTransferred, &Overlapped))
    {
        errorW(L"fault: WriteFile ctx=%s", ctx->str().c_str());
        return FspNtStatusFromWin32(::GetLastError());
    }

    traceW(L"success: WriteFile argBytesTransferred=%lu", *argBytesTransferred);

    // Write に必要な部分のみ同期しているため、リモートのファイルサイズを優先(true)する

    return this->updateFileInfo(START_CALLER ctx, pFileInfo, true);
    //return GetFileInfoInternal(Handle, pFileInfo);
}

#pragma warning(suppress: 4100)
NTSTATUS CSDriver::SetDelete(FileContext* ctx, PCWSTR argFileName, BOOLEAN argDeleteFile)
{
    NEW_LOG_BLOCK();

    traceW(L"argFileName=%s argDeleteFile=%s ctx=%s", argFileName, BOOL_CSTRW(argDeleteFile), ctx->str().c_str());

    if (!argDeleteFile)
    {
        // 削除のキャンセルは難しいので無視

        errorW(L"Can not cancel to delete ctx=%s", ctx->str().c_str());
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    const auto objKey{ ctx->getObjectKey() };

    switch (ctx->getDirEntry()->mFileType)
    {
        case FileTypeEnum::Directory:
        {
            // SetDelete であればエラーを返却できるので、ディレクトリの削除はここで実施してしまう

            DirEntryListType dirEntryList;

            if (!mDevice->listObjects(START_CALLER objKey, &dirEntryList))
            {
                errorW(L"fault: listObjects objKey=%s", objKey.c_str());
                return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
            }

            // ディレクトリを削除できるかチェック

            switch (mRuntimeEnv->DeleteDirCondition)
            {
                case 1:
                {
                    // サブディレクトリがある場合は削除不可

                    const auto it = std::find_if(dirEntryList.cbegin(), dirEntryList.cend(), [](const auto& item)
                    {
                        return FA_IS_DIR(item->mFileInfo.FileAttributes);
                    });

                    if (it != dirEntryList.cend())
                    {
                        traceW(L"dir not empty");
                        return STATUS_CANNOT_DELETE;
                        //return STATUS_DIRECTORY_NOT_EMPTY;
                    }

                    break;
                }

                case 2:
                {
                    // 空のディレクトリ以外は削除不可

                    if (!dirEntryList.empty())
                    {
                        traceW(L"dir not empty");
                        return STATUS_CANNOT_DELETE;
                        //return STATUS_DIRECTORY_NOT_EMPTY;
                    }

                    break;
                }
            }

            // リモートのディレクトリを削除

            const auto& refWinPath{ ctx->getWinPath() };

            while (true)
            {
                //int numDelete = 0;
                std::list<std::wstring> delKeys;

                for (const auto& dirEntry: dirEntryList)
                {
                    APP_ASSERT(dirEntry->mName != L"." && dirEntry->mName != L"..");

                    if (FA_IS_DIR(dirEntry->mFileInfo.FileAttributes))
                    {
                        // 削除開始からここまでの間にディレクトリが作成される可能性を考え
                        // 存在したら無視

                        continue;
                    }

                    // ローカルのキャッシュ・ファイルを削除

                    const auto winPath{ refWinPath / dirEntry->getFileNameBuf() };

                    std::filesystem::path cacheFilePath;

                    if (!resolveCacheFilePath(mRuntimeEnv->CacheDataDir, winPath, &cacheFilePath))
                    {
                        errorW(L"fault: resolveCacheFilePath winPath=%s", winPath.c_str());
                        return FspNtStatusFromWin32(ERROR_WRITE_FAULT);
                    }

                    traceW(L"delete cacheFilePath=%s", cacheFilePath.c_str());

                    if (!::DeleteFileW(cacheFilePath.c_str()))
                    {
                        const auto lerr = ::GetLastError();
                        if (lerr != ERROR_FILE_NOT_FOUND)
                        {
                            errorW(L"fault: DeleteFileW lerr=%lu cacheFilePath=%s", lerr, cacheFilePath.c_str());
                            return FspNtStatusFromWin32(lerr);
                        }
                    }

                    const auto delKey{ objKey.append(dirEntry->mName).key() };

                    traceW(L"add del list delKey=%s", delKey.c_str());
                    delKeys.push_back(delKey);
                }

                if (delKeys.empty())
                {
                    break;
                }
                else
                {
                    if (!mDevice->deleteObjects(START_CALLER objKey.bucket(), delKeys))
                    {
                        errorW(L"fault: deleteObjects bucket=%s keys=%s", objKey.c_str(), JoinStrings(delKeys, L',', true).c_str());
                        return STATUS_CANNOT_DELETE;
                    }
                }

                //
                // 一度の listObjects では最大数の制限があるかもしれないので、削除する
                // 対象がなくなるまで繰り返す
                //

                if (!mDevice->listObjects(START_CALLER objKey, &dirEntryList))
                {
                    errorW(L"fault: listObjects objKey=%s", objKey.c_str());
                    return STATUS_CANNOT_DELETE;
                }
            }

            break;
        }

        case FileTypeEnum::File:
        {
            /*
            https://stackoverflow.com/questions/36217150/deleting-a-file-based-on-disk-id

            SetFileInformationByHandle を FILE_DISPOSITION_INFO と共に使用すると、
            開いているハンドルを持つファイルを、すべてのハンドルが閉じられたときに削除されるように設定できます。
            */

            HANDLE Handle = ctx->getWritableHandle();

            FILE_DISPOSITION_INFO DispositionInfo{};

            DispositionInfo.DeleteFile = argDeleteFile;

            if (!::SetFileInformationByHandle(Handle, FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo))
            {
                errorW(L"fault: SetFileInformationByHandle ctx=%s", ctx->str().c_str());
                return FspNtStatusFromWin32(::GetLastError());
            }

            break;
        }
    }

    if (!mDevice->deleteObject(START_CALLER objKey))
    {
        errorW(L"fault: deleteObject objKey=%s", objKey.c_str());
        return STATUS_CANNOT_DELETE;
    }

    return STATUS_SUCCESS;
}

}   // namespace CSEDRV

// EOF