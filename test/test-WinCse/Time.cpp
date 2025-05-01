#include "WinCseLib.h"
#include <iostream>

using namespace CSELIB;

static bool PathToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite)
{
    FileHandle hFile = ::CreateFileW
    (
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if(hFile.invalid())
    {
        return false;
    }

    return ::GetFileTime(hFile.handle(), pFtCreate, pFtAccess, pFtWrite);
}

void t_WinCseLib_Time()
{
    auto hosts = STCTimeToUTCMillisW(L"C:\\Windows\\System32\\Drivers\\etc\\hosts");
    std::cout << hosts << std::endl;

    /*
    1670547095000
    1670547095000
    133150206954262405
    1670547095426
    1670547095426
    133150206954260000
    133150206954260000
    */

    FILETIME ftCreate, ftAccess, ftWrite;
    PathToWinFileTimes(L"C:\\Windows\\System32\\Drivers\\etc\\hosts", &ftCreate, &ftAccess, &ftWrite);

    auto crtW100ns = WinFileTimeToWinFileTime100ns(ftCreate);
    std::cout << crtW100ns << std::endl;

    auto crtUtcMSec = WinFileTimeToUtcMillis(ftCreate);
    std::cout << crtUtcMSec << std::endl;

    auto utcMSec = WinFileTime100nsToUtcMillis(crtW100ns);
    std::cout << utcMSec << std::endl;

    auto utcW100ns = UtcMillisToWinFileTime100ns(utcMSec);
    std::cout << utcW100ns << std::endl;

    FILETIME ftTemp;
    UtcMillisToWinFileTime(utcMSec, &ftTemp);

    auto tempW100ns = WinFileTimeToWinFileTime100ns(ftTemp);
    std::cout << tempW100ns << std::endl;
}

// EOF