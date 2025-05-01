// util-GetBasicInfo.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include <Windows.h>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

// ミリ秒から100ナノ秒単位への変換
#define HUNDRED_NANOSECONDS_PER_MILLISECOND		(10000ULL)

// 1601年1月1日から1970年1月1日までのオフセット (ミリ秒)
#define EPOCH_DIFFERENCE						(11644473600000ULL)

// 1601年1月1日から1970年1月1日までのオフセット (100ナノ秒単位)
#define EPOCH_DIFFERENCE_100NS					(116444736000000000ULL)

INT64 WinFileTimeToWinFileTime100ns(const FILETIME& ft)
{
    return ((PLARGE_INTEGER)&ft)->QuadPart;
}

INT64 WinFileTimeToUtcMillis(const FILETIME &ft)
{
    // 差を引き、ミリ秒単位に変換
    return (WinFileTimeToWinFileTime100ns(ft) - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

INT64 WinFileTime100nsToUtcMillis(INT64 ft100ns)
{
    return (ft100ns - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

std::wstring UtcMilliToLocalTimeStringW(INT64 milliseconds)
{
    // ミリ秒を chrono::milliseconds に変換
    const std::chrono::milliseconds ms{ milliseconds };

    // ミリ秒から chrono::system_clock::time_point に変換
    const auto tp{ std::chrono::system_clock::time_point(ms) };

    // time_point を std::time_t に変換
    const auto time = std::chrono::system_clock::to_time_t(tp);

    // ミリ秒部分を取得
    const int fractional_seconds = milliseconds % 1000;

    // std::time_t を std::tm に変換
    std::tm tm;
    //gmtime_s(&tm, &time);
    localtime_s(&tm, &time);

    // std::tm を文字列にフォーマット
    std::wostringstream ss;
    ss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
    ss << "." << std::setw(3) << std::setfill(L'0') << fractional_seconds;

    return ss.str();
}

std::wstring WinFileTimeToLocalTimeStringW(const FILETIME &ft)
{
    return UtcMilliToLocalTimeStringW(WinFileTimeToUtcMillis(ft));
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: " << argv[0] << L" path" << std::endl;
        return EXIT_FAILURE;
    }

    auto handle = ::CreateFile(
        argv[1],
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (handle == INVALID_HANDLE_VALUE)
    {
        const auto lerr = ::GetLastError();
        std::wcerr << L"LastError: " << lerr << std::endl;
        return EXIT_FAILURE;
    }

    /*
typedef struct _BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD dwVolumeSerialNumber;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD nNumberOfLinks;
    DWORD nFileIndexHigh;
    DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION
    */

    BY_HANDLE_FILE_INFORMATION info{};

    if (::GetFileInformationByHandle(handle, &info))
    {
        LARGE_INTEGER fileSize{};
        fileSize.HighPart = info.nFileSizeHigh;
        fileSize.LowPart = info.nFileSizeLow;

        std::wcout << L"FileSize=" << fileSize.QuadPart << std::endl;
        std::wcout << L"dwFileAttributes=" << info.dwFileAttributes << std::endl;

        std::wcout << L"[Windows]" << std::endl;
        std::wcout << L"ftCreationTime=" << WinFileTimeToWinFileTime100ns(info.ftCreationTime) << std::endl;
        std::wcout << L"ftLastWriteTime=" << WinFileTimeToWinFileTime100ns(info.ftLastWriteTime) << std::endl;
        std::wcout << L"ftLastAccessTime=" << WinFileTimeToWinFileTime100ns(info.ftLastAccessTime) << std::endl;

        std::wcout << L"[Utc Millis]" << std::endl;
        std::wcout << L"ftCreationTime=" << WinFileTimeToUtcMillis(info.ftCreationTime) << std::endl;
        std::wcout << L"ftLastWriteTime=" << WinFileTimeToUtcMillis(info.ftLastWriteTime) << std::endl;
        std::wcout << L"ftLastAccessTime=" << WinFileTimeToUtcMillis(info.ftLastAccessTime) << std::endl;

        std::wcout << L"[Local Time]" << std::endl;
        std::wcout << L"ftCreationTime=" << WinFileTimeToLocalTimeStringW(info.ftCreationTime) << std::endl;
        std::wcout << L"ftLastWriteTime=" << WinFileTimeToLocalTimeStringW(info.ftLastWriteTime) << std::endl;
        std::wcout << L"ftLastAccessTime=" << WinFileTimeToLocalTimeStringW(info.ftLastAccessTime) << std::endl;
    }

    ::CloseHandle(handle);

    return EXIT_SUCCESS;
}

// EOF