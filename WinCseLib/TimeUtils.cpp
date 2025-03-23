#include "WinCseLib.h"
#include <sstream>
#include <iomanip>


namespace WinCseLib {

// UTC ミリ秒を YYYY-MM-DD HH:MI:SS.NNN 文字列に変換
std::wstring UtcMilliToLocalTimeStringW(uint64_t milliseconds)
{
	namespace chrono = std::chrono;

	// ミリ秒を chrono::milliseconds に変換
	const chrono::milliseconds ms{ milliseconds };

	// ミリ秒から chrono::system_clock::time_point に変換
	const auto tp{ chrono::system_clock::time_point(ms) };

	// time_point を std::time_t に変換
	const std::time_t time = chrono::system_clock::to_time_t(tp);

	// ミリ秒部分を取得
	const int fractional_seconds = milliseconds % 1000;

	// std::time_t を std::tm に変換
	std::tm tm;
	//gmtime_s(&tm, &time);
	localtime_s(&tm, &time);

	// std::tm を文字列にフォーマット
	std::wstringstream ss;
	ss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
	ss << "." << std::setw(3) << std::setfill(L'0') << fractional_seconds;

	return ss.str();
}

// 100ns 単位の FILETIME を文字列に変換
std::wstring WinFileTime100nsToLocalTimeStringW(uint64_t ft100ns)
{
	return UtcMilliToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns));
}

std::string WinFileTime100nsToLocalTimeStringA(uint64_t ft100ns)
{
	return WC2MB(UtcMilliToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns)));
}

// time_point を UTC のミリ秒に変換
long long int TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp)
{
	// エポックからの経過時間をミリ秒単位で取得
	const auto duration{ tp.time_since_epoch() };

	// 秒単位のUnix時間に変換
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// time_point を UTC の秒に変換
long long int TimePointToUtcSecs(const std::chrono::system_clock::time_point& tp)
{
	// エポックからの経過時間をミリ秒単位で取得
	const auto duration{ tp.time_since_epoch() };

	// 秒単位のUnix時間に変換
	return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

// time_point を文字列に変換
std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp)
{
	return UtcMilliToLocalTimeStringW(TimePointToUtcMillis(tp));
}

// 現在の時刻を UTC のミリ秒で取得
uint64_t GetCurrentUtcMillis()
{
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);

	return WinFileTimeToUtcMillis(ft);
}

uint64_t STCTimeToUTCMilliSecW(const std::wstring& path)
{
	APP_ASSERT(!path.empty());

	//
	struct _stat st;
	if (_wstat(path.c_str(), &st) != 0)
	{
		return 0;
	}

	return st.st_ctime * (time_t)1000;
}

uint64_t STCTimeToUTCMilliSecA(const std::string& path)
{
	return STCTimeToUTCMilliSecW(MB2WC(path));
}

uint64_t STCTimeToWinFileTimeW(const std::wstring& path)
{
	return UtcMillisToWinFileTime100ns(STCTimeToUTCMilliSecW(path));
}

uint64_t STCTimeToWinFileTimeA(const std::string& path)
{
	return UtcMillisToWinFileTime100ns(STCTimeToUTCMilliSecA(path));
}

// UTC のミリ秒を FILETIME 構造体に変換
void UtcMillisToWinFileTime(uint64_t utcMilliseconds, FILETIME* ft)
{
	APP_ASSERT(ft);

	const auto fileTime = UtcMillisToWinFileTime100ns(utcMilliseconds);

	// FILETIME構造体に変換
	ft->dwLowDateTime = (DWORD)(fileTime & 0xFFFFFFFF);
	ft->dwHighDateTime = (DWORD)(fileTime >> 32);
}

// FILETIME 構造体を 100ns 単位の uitn64_t 値に変換
uint64_t WinFileTimeToWinFileTime100ns(const FILETIME& ft)
{
	return ((PLARGE_INTEGER)&ft)->QuadPart;
}

void WinFileTime100nsToWinFile(uint64_t ft100ns, FILETIME* ft)
{
	((PLARGE_INTEGER)ft)->QuadPart = ft100ns;
}

// ミリ秒から100ナノ秒単位への変換
#define HUNDRED_NANOSECONDS_PER_MILLISECOND		(10000ULL)

// 1601年1月1日から1970年1月1日までのオフセット (ミリ秒)
#define EPOCH_DIFFERENCE						(11644473600000ULL)

// 1601年1月1日から1970年1月1日までのオフセット (100ナノ秒単位)
#define EPOCH_DIFFERENCE_100NS					(116444736000000000ULL)

// UTC のミリ秒を Windows のファイル時刻に変換
uint64_t UtcMillisToWinFileTime100ns(uint64_t utcMilliseconds)
{
	return (utcMilliseconds + EPOCH_DIFFERENCE) * HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows のファイル時刻を UTC のミリ秒 に変換
uint64_t WinFileTime100nsToUtcMillis(uint64_t ft100ns)
{
	return (ft100ns - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows のファイル時刻を UTC のミリ秒に変換
uint64_t WinFileTimeToUtcMillis(const FILETIME &ft)
{
	// 差を引き、ミリ秒単位に変換
	return (WinFileTimeToWinFileTime100ns(ft) - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

} // WinCseLib

// EOF