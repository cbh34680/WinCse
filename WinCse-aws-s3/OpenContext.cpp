#include "OpenContext.hpp"

using namespace WCSE;


NTSTATUS OpenContext::openFileHandle(CALLER_ARG DWORD argDesiredAccess, DWORD argCreationDisposition) noexcept
{
    NEW_LOG_BLOCK();
    APP_ASSERT(isFile());
    APP_ASSERT(mObjKey.meansFile());
    APP_ASSERT(mFile.invalid());

    const auto localPath{ this->getCacheFilePath() };

    const DWORD dwDesiredAccess = mGrantedAccess | argDesiredAccess;

    ULONG CreateFlags = 0;
    //CreateFlags = FILE_FLAG_BACKUP_SEMANTICS;             // ディレクトリは操作しない

    if (mCreateOptions & FILE_DELETE_ON_CLOSE)
        CreateFlags |= FILE_FLAG_DELETE_ON_CLOSE;

    HANDLE Handle = ::CreateFileW(localPath.c_str(),
        dwDesiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
        argCreationDisposition, CreateFlags, 0);

    if (Handle == INVALID_HANDLE_VALUE)
    {
        const auto lerr = ::GetLastError();
        traceW(L"fault: CreateFileW lerr=%lu", lerr);

        return FspNtStatusFromWin32(lerr);
    }

    mFile = Handle;

    return STATUS_SUCCESS;
}

// EOF