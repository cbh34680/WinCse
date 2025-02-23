#include "WinCseLib.h"
#include <Windows.h>


namespace WinCseLib {

uint64_t STCTimeToUTCMilliSecW(const std::wstring& path)
{
	APP_ASSERT(!path.empty());

	//
	struct _stat st;
	if (_wstat(path.c_str(), &st) != 0)
	{
		return 0;
	}

	// time_t を time_point に変換
	const auto time_point = std::chrono::system_clock::from_time_t(st.st_ctime);

	// エポックからの経過時間をミリ秒単位で取得
	auto duration = time_point.time_since_epoch();

	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

uint64_t STCTimeToUTCMilliSecA(const std::string& path)
{
	return STCTimeToUTCMilliSecW(MB2WC(path));
}

uint64_t STCTimeToWinFileTimeW(const std::wstring& path)
{
	return UtcMillisToWinFileTimeIn100ns(STCTimeToUTCMilliSecW(path));
}

uint64_t STCTimeToWinFileTimeA(const std::string& path)
{
	return UtcMillisToWinFileTimeIn100ns(STCTimeToUTCMilliSecA(path));
}

// UTC のミリ秒を Windows のファイル時刻に変換
uint64_t UtcMillisToWinFileTimeIn100ns(uint64_t utcMilliseconds)
{
	// 1601年1月1日からのオフセット
	static const uint64_t EPOCH_DIFFERENCE = 11644473600000LL; // ミリ秒

	// ミリ秒を100ナノ秒単位に変換し、オフセットを加算
	static const uint64_t HUNDRED_NANOSECONDS_PER_MILLISECOND = 10000;

	return (utcMilliseconds + EPOCH_DIFFERENCE) * HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// FILETIME 構造体を 100ns 単位の uitn64_t 値に変換
uint64_t WinFileTimeIn100ns(const FILETIME& ft)
{
	ULARGE_INTEGER ull{};

	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;

	return ull.QuadPart;
}

// UTC のミリ秒を FILETIME 構造体に変換
void UtcMillisToWinFileTime(uint64_t utcMilliseconds, FILETIME* ft)
{
	APP_ASSERT(ft);

	const auto fileTime = UtcMillisToWinFileTimeIn100ns(utcMilliseconds);

	// FILETIME構造体に変換
	ft->dwLowDateTime = (DWORD)(fileTime & 0xFFFFFFFF);
	ft->dwHighDateTime = (DWORD)(fileTime >> 32);
}

// Windows のファイル時刻を UTC のミリ秒に変換
uint64_t WinFileTimeToUtcMillis(const FILETIME &ft)
{
	// FILETIME を 64 ビットの整数に変換
	ULARGE_INTEGER ull = {};

	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;

	// 1601 年 1 月 1 日からの経過時間を 100 ナノ秒単位で保持しているため、1970 年 1 月 1 日との差を計算
	static const uint64_t UNIX_EPOCH_DIFFERENCE = 116444736000000000ULL;

	// 差を引き、ミリ秒単位に変換
	return (ull.QuadPart - UNIX_EPOCH_DIFFERENCE) / 10000ULL;
}

// time_point を UTC の秒に変換
long long int TimePointToUtcSecs(const std::chrono::system_clock::time_point& tp)
{
	// エポックからの経過時間をミリ秒単位で取得
	auto duration = tp.time_since_epoch();

	// ミリ秒単位のUnix時間に変換
	//auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

	// 秒単位のUnix時間に変換
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

	return seconds;
}

// ファイルハンドルから FILETIME の値を取得
bool HandleToWinFileTimes(const std::wstring& path, FILETIME* pFtCreate, FILETIME* pFtAccess, FILETIME* pFtWrite)
{
	HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, 0, NULL);

	if(hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	BOOL ret = ::GetFileTime(hFile, pFtCreate, pFtAccess, pFtWrite);

	CloseHandle(hFile);

	return ret;
}

// 現在の時刻を UTC のミリ秒で取得
uint64_t GetCurrentUtcMillis()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);

	return WinFileTimeToUtcMillis(ft);
}

} // WinCseLib

// EOF