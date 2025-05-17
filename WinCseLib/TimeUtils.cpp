#include "WinCseLib.h"
#include <iomanip>

namespace CSELIB {

// UTC ミリ秒を YYYY-MM-DD HH:MI:SS.NNN 文字列に変換
std::wstring UtcMillisToLocalTimeStringW(UTC_MILLIS_T argUtcMillis)
{
	// ミリ秒を chrono::milliseconds に変換
	const std::chrono::milliseconds ms{ argUtcMillis };

	// ミリ秒から chrono::system_clock::time_point に変換
	const auto tp{ std::chrono::system_clock::time_point(ms) };

	// time_point を std::time_t に変換
	const auto time = std::chrono::system_clock::to_time_t(tp);

	// ミリ秒部分を取得
	const int fractional_seconds = argUtcMillis % 1000;

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

// 100ns 単位の FILETIME を文字列に変換
std::wstring WinFileTime100nsToLocalTimeStringW(FILETIME_100NS_T ft100ns)
{
	return UtcMillisToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns));
}

std::string WinFileTime100nsToLocalTimeStringA(FILETIME_100NS_T ft100ns)
{
	return WC2MB(UtcMillisToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns)));
}

// time_point を UTC のミリ秒に変換
UTC_MILLIS_T TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp)
{
	// エポックからの経過時間をミリ秒単位で取得

	const auto duration{ tp.time_since_epoch() };

	// 秒単位のUnix時間に変換

	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// time_point を文字列に変換
std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp)
{
	return UtcMillisToLocalTimeStringW(TimePointToUtcMillis(tp));
}

// 現在の時刻を UTC のミリ秒で取得
UTC_MILLIS_T GetCurrentUtcMillis()
{
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);

	return WinFileTimeToUtcMillis(ft);
}

FILETIME_100NS_T GetCurrentWinFileTime100ns()
{
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);

	return WinFileTimeToWinFileTime100ns(ft);
}

UTC_MILLIS_T STCTimeToUTCMillisW(const std::wstring& path)
{
	APP_ASSERT(!path.empty());

	struct _stat st;
	if (_wstat(path.c_str(), &st) != 0)
	{
		return 0;
	}

	return st.st_ctime * (time_t)1000;
}

UTC_MILLIS_T STCTimeToUTCMilliSecA(const std::string& path)
{
	return STCTimeToUTCMillisW(MB2WC(path));
}

FILETIME_100NS_T STCTimeToWinFileTime100nsW(const std::wstring& path)
{
	return UtcMillisToWinFileTime100ns(STCTimeToUTCMillisW(path));
}

// UTC のミリ秒を FILETIME 構造体に変換
void UtcMillisToWinFileTime(UTC_MILLIS_T argUtcMillis, FILETIME* pFileTime)
{
	const auto ft100ns = UtcMillisToWinFileTime100ns(argUtcMillis);

	WinFileTime100nsToWinFile(ft100ns, pFileTime);
}

// FILETIME 構造体を 100ns 単位の uitn64_t 値に変換
FILETIME_100NS_T WinFileTimeToWinFileTime100ns(const FILETIME& ft)
{
	//return ((PLARGE_INTEGER)&ft)->QuadPart;

	ULARGE_INTEGER ularge{};

	ularge.LowPart = ft.dwLowDateTime;				// 低位の 32 ビット
	ularge.HighPart = ft.dwHighDateTime;			// 高位の 32 ビット

	return ularge.QuadPart;
}

void WinFileTime100nsToWinFile(FILETIME_100NS_T ft100ns, FILETIME* pFileTime)
{
	//((PLARGE_INTEGER)pFileTime)->QuadPart = ft100ns;

	ULARGE_INTEGER ularge{};
	ularge.QuadPart = static_cast<ULONGLONG>(ft100ns);

	pFileTime->dwLowDateTime = ularge.LowPart;		// 低位32ビット
	pFileTime->dwHighDateTime = ularge.HighPart;	// 高位32ビット
}

// ミリ秒から100ナノ秒単位への変換
#define HUNDRED_NANOSECONDS_PER_MILLISECOND		(10000ULL)

// 1601年1月1日から1970年1月1日までのオフセット (ミリ秒)
#define EPOCH_DIFFERENCE						(11644473600000ULL)

// 1601年1月1日から1970年1月1日までのオフセット (100ナノ秒単位)
#define EPOCH_DIFFERENCE_100NS					(116444736000000000ULL)

// UTC のミリ秒を Windows のファイル時刻に変換
FILETIME_100NS_T UtcMillisToWinFileTime100ns(UTC_MILLIS_T argUtcMillis)
{
	return (argUtcMillis + EPOCH_DIFFERENCE) * HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows のファイル時刻を UTC のミリ秒 に変換
UTC_MILLIS_T WinFileTime100nsToUtcMillis(FILETIME_100NS_T ft100ns)
{
	return (ft100ns - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows のファイル時刻を UTC のミリ秒に変換
UTC_MILLIS_T WinFileTimeToUtcMillis(const FILETIME& ft)
{
	// 差を引き、ミリ秒単位に変換
	return (WinFileTimeToWinFileTime100ns(ft) - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows のファイル時刻をローカル時間文字列に変換
std::wstring WinFileTimeToLocalTimeStringW(const FILETIME& ft)
{
	return UtcMillisToLocalTimeStringW(WinFileTimeToUtcMillis(ft));
}

} // CSELIB

// EOF