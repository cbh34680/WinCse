#include "AwsS3.hpp"


using namespace WinCseLib;


std::wstring OpenContext::getLocalPath() const
{
    return mCacheDataDir + L'\\' + EncodeFileNameToLocalNameW(getRemotePath());
}

bool OpenContext::openLocalFile(const DWORD argDesiredAccess, const DWORD argCreationDisposition)
{
    NEW_LOG_BLOCK();

    // キャッシュ・ファイルを開き、HANDLE をコンテキストに保存

    ULONG CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;
    if (mCreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    const DWORD dwDesiredAccess = mGrantedAccess | argDesiredAccess;

    mLocalFile = ::CreateFileW(getLocalPath().c_str(),
        dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, argCreationDisposition, CreateFlags, NULL);

    if (mLocalFile == INVALID_HANDLE_VALUE)
    {
        traceW(L"fault: CreateFileW");
        return false;
    }

    StatsIncr(_CreateFile);

    return true;
}

bool OpenContext::setLocalFileTime(UINT64 argCreationTime)
{
    NEW_LOG_BLOCK();

    APP_ASSERT(mLocalFile != INVALID_HANDLE_VALUE);

    FILETIME ft;
    WinFileTime100nsToWinFile(argCreationTime, &ft);

    FILETIME ftNow;
    ::GetSystemTimeAsFileTime(&ftNow);

    if (!::SetFileTime(mLocalFile, &ft, &ftNow, &ft))
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: SetFileTime lerr=%ld", lerr);

        return false;
    }

    return true;
}

void OpenContext::closeLocalFile()
{
    if (mLocalFile != INVALID_HANDLE_VALUE)
    {
        StatsIncr(_CloseHandle_File);
        ::CloseHandle(mLocalFile);

        mLocalFile = INVALID_HANDLE_VALUE;
    }
}

IOpenContext* AwsS3::open(CALLER_ARG const ObjectKey& argObjKey,
    const FSP_FSCTL_FILE_INFO& FileInfo,
    const UINT32 CreateOptions, const UINT32 GrantedAccess)
{
    StatsIncr(open);

    NEW_LOG_BLOCK();

    // DoOpen() から呼び出されるが、ファイルを開く=ダウンロードになってしまうため
    // ここでは UParam に情報のみを保存し、DoRead() から呼び出される readFile() で
    // ファイルのダウンロード処理 (キャッシュ・ファイル) を行う。

    OpenContext* ctx = new OpenContext(mStats, mCacheDataDir, argObjKey, FileInfo, CreateOptions, GrantedAccess);
    APP_ASSERT(ctx);

    return ctx;
}

void AwsS3::close(CALLER_ARG WinCseLib::IOpenContext* argOpenContext)
{
    StatsIncr(close);
    NEW_LOG_BLOCK();

    OpenContext* ctx = dynamic_cast<OpenContext*>(argOpenContext);
    APP_ASSERT(ctx);

    delete ctx;
}

// EOF