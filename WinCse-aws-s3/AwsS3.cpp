#include "AwsS3.hpp"
#include <filesystem>

using namespace WinCseLib;

//
// AwsS3
//
WinCseLib::ICSDevice* NewCSDevice(
    const wchar_t* argTempDir, const wchar_t* argIniSection,
    NamedWorker argWorkers[])
{
    std::unordered_map<std::wstring, IWorker*> workers;

    if (NamedWorkersToMap(argWorkers, &workers) <= 0)
    {
        return nullptr;
    }

    for (const auto* key: { L"delayed", L"idle", L"timer", })
    {
        if (workers.find(key) == workers.end())
        {
            return nullptr;
        }
    }

    return new AwsS3(argTempDir, argIniSection, std::move(workers));
}

AwsS3::AwsS3(const std::wstring& argTempDir, const std::wstring& argIniSection,
    std::unordered_map<std::wstring, IWorker*>&& argWorkers)
    :
    mTempDir(argTempDir), mIniSection(argIniSection),
    mWorkers(std::move(argWorkers))
{
    APP_ASSERT(std::filesystem::exists(argTempDir));
    APP_ASSERT(std::filesystem::is_directory(argTempDir));

    mStats = &mStats_;
}

AwsS3::~AwsS3()
{
    NEW_LOG_BLOCK();

    this->OnSvcStop();

    // 必要ないが、デバッグ時のメモリ・リーク調査の邪魔になるので

    clearBucketCache(START_CALLER0);
    clearObjectCache(START_CALLER0);

    mRefFile.close();
    mRefDir.close();
}

bool AwsS3::isInBucketFilters(const std::wstring& arg)
{
    if (mBucketFilters.empty())
    {
        return true;
    }

    const auto it = std::find_if(mBucketFilters.begin(), mBucketFilters.end(), [&arg](const auto& re)
    {
        return std::regex_match(arg, re);
    });

    return it != mBucketFilters.end();
}

DirInfoType AwsS3::makeDirInfo_attr(const std::wstring& argFileName, const UINT64 argFileTime, const UINT32 argFileAttributes)
{
    APP_ASSERT(!argFileName.empty());

    auto dirInfo = makeDirInfo(argFileName);
    APP_ASSERT(dirInfo);

    UINT32 fileAttributes = argFileAttributes | mDefaultFileAttributes;

    if (argFileName != L"." && argFileName != L".." && argFileName[0] == L'.')
    {
        // 隠しファイル

        fileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }

    dirInfo->FileInfo.FileAttributes = fileAttributes;

    dirInfo->FileInfo.CreationTime = argFileTime;
    dirInfo->FileInfo.LastAccessTime = argFileTime;
    dirInfo->FileInfo.LastWriteTime = argFileTime;
    dirInfo->FileInfo.ChangeTime = argFileTime;

    return dirInfo;
}

DirInfoType AwsS3::makeDirInfo_byName(const std::wstring& argFileName, const UINT64 argFileTime)
{
    APP_ASSERT(!argFileName.empty());

    const auto lastChar = argFileName[argFileName.length() - 1];

    return makeDirInfo_attr(argFileName, argFileTime, lastChar == L'/' ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL);
}

DirInfoType AwsS3::makeDirInfo_dir(const std::wstring& argFileName, const UINT64 argFileTime)
{
    return makeDirInfo_attr(argFileName, argFileTime, FILE_ATTRIBUTE_DIRECTORY);
}

NTSTATUS AwsS3::getHandleFromContext(CALLER_ARG
    WinCseLib::CSDeviceContext* argCSDeviceContext,
    const DWORD argDesiredAccess, const DWORD argCreationDisposition, PHANDLE pHandle)
{
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argCSDeviceContext);
    APP_ASSERT(ctx);
    APP_ASSERT(ctx->isFile());

    const auto remotePath{ ctx->mObjKey.str() };

    traceW(L"Context=%p ObjectKey=%s HANDLE=%p, RemotePath=%s DesiredAccess=%lu CreationDisposition=%lu",
        ctx, ctx->mObjKey.c_str(), ctx->mFile.handle(), remotePath.c_str(),
        argDesiredAccess, argCreationDisposition);

    // ファイル名への参照を登録

    UnprotectedShare<PrepareLocalCacheFileShared> unsafeShare(&mGuardPrepareLocalCache, remotePath);  // 名前への参照を登録
    {
        const auto safeShare{ unsafeShare.lock() };                                 // 名前のロック

        if (ctx->mFile.invalid())
        {
            // AwsS3::open() 後の初回の呼び出し

            NTSTATUS ntstatus = ctx->openFileHandle(CONT_CALLER argDesiredAccess, argCreationDisposition);
            if (!NT_SUCCESS(ntstatus))
            {
                traceW(L"fault: openFileHandle");
                return ntstatus;
            }

            APP_ASSERT(ctx->mFile.valid());
        }
    }   // 名前のロックを解除 (safeShare の生存期間)

    *pHandle = ctx->mFile.handle();

    return STATUS_SUCCESS;
}

//
// OpenContext
//
NTSTATUS OpenContext::openFileHandle(CALLER_ARG const DWORD argDesiredAccess, const DWORD argCreationDisposition)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(isFile());
    APP_ASSERT(mObjKey.meansFile());

    std::wstring localPath;
    if (!getCacheFilePath(&localPath))
    {
        traceW(L"fault: getCacheFilePath");
        //return STATUS_OBJECT_NAME_NOT_FOUND;
        return FspNtStatusFromWin32(ERROR_FILE_NOT_FOUND);
    }

    const DWORD dwDesiredAccess = mGrantedAccess | argDesiredAccess;

    ULONG CreateFlags = 0;
    //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // ディレクトリは操作しない

    if (mCreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    HANDLE hFile = ::CreateFileW(localPath.c_str(),
        dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        argCreationDisposition, CreateFlags, 0);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    mFile = hFile;

    return STATUS_SUCCESS;
}

//
// FileOutputParams
//
std::wstring FileOutputParams::str() const
{
    std::wstring sCreationDisposition;

    switch (mCreationDisposition)
    {
        case CREATE_ALWAYS:     sCreationDisposition = L"CREATE_ALWAYS";     break;
        case CREATE_NEW:        sCreationDisposition = L"CREATE_NEW";        break;
        case OPEN_ALWAYS:       sCreationDisposition = L"OPEN_ALWAYS";       break;
        case OPEN_EXISTING:     sCreationDisposition = L"OPEN_EXISTING";     break;
        case TRUNCATE_EXISTING: sCreationDisposition = L"TRUNCATE_EXISTING"; break;
        default: APP_ASSERT(0);
    }

    std::wstringstream ss;

    ss << L"mPath=";
    ss << mPath;
    ss << L" mCreationDisposition=";
    ss << sCreationDisposition;
    ss << L" mOffset=";
    ss << mOffset;
    ss << L" mLength=";
    ss << mLength;
    ss << L" mSpecifyRange=";
    ss << BOOL_CSTRW(mSpecifyRange);

    return ss.str();
}

// EOF
