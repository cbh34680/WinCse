#include "WinCseLib.h"
#include <iomanip>

namespace CSELIB {

// UTC �~���b�� YYYY-MM-DD HH:MI:SS.NNN ������ɕϊ�
std::wstring UtcMillisToLocalTimeStringW(UTC_MILLIS_T argUtcMillis)
{
	// �~���b�� chrono::milliseconds �ɕϊ�
	const std::chrono::milliseconds ms{ argUtcMillis };

	// �~���b���� chrono::system_clock::time_point �ɕϊ�
	const auto tp{ std::chrono::system_clock::time_point(ms) };

	// time_point �� std::time_t �ɕϊ�
	const auto time = std::chrono::system_clock::to_time_t(tp);

	// �~���b�������擾
	const int fractional_seconds = argUtcMillis % 1000;

	// std::time_t �� std::tm �ɕϊ�
	std::tm tm;
	//gmtime_s(&tm, &time);
	localtime_s(&tm, &time);

	// std::tm �𕶎���Ƀt�H�[�}�b�g
	std::wostringstream ss;

	ss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
	ss << "." << std::setw(3) << std::setfill(L'0') << fractional_seconds;

	return ss.str();
}

// 100ns �P�ʂ� FILETIME �𕶎���ɕϊ�
std::wstring WinFileTime100nsToLocalTimeStringW(FILETIME_100NS_T ft100ns)
{
	return UtcMillisToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns));
}

std::string WinFileTime100nsToLocalTimeStringA(FILETIME_100NS_T ft100ns)
{
	return WC2MB(UtcMillisToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns)));
}

// time_point �� UTC �̃~���b�ɕϊ�
UTC_MILLIS_T TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp)
{
	// �G�|�b�N����̌o�ߎ��Ԃ��~���b�P�ʂŎ擾

	const auto duration{ tp.time_since_epoch() };

	// �b�P�ʂ�Unix���Ԃɕϊ�

	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// time_point �𕶎���ɕϊ�
std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp)
{
	return UtcMillisToLocalTimeStringW(TimePointToUtcMillis(tp));
}

FILETIME_100NS_T TimePointToWinFileTime100ns(const std::chrono::system_clock::time_point& tp)
{
	return UtcMillisToWinFileTime100ns(TimePointToUtcMillis(tp));
}

// ���݂̎����� UTC �̃~���b�Ŏ擾
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

// UTC �̃~���b�� FILETIME �\���̂ɕϊ�
void UtcMillisToWinFileTime(UTC_MILLIS_T argUtcMillis, FILETIME* pFileTime)
{
	const auto ft100ns = UtcMillisToWinFileTime100ns(argUtcMillis);

	WinFileTime100nsToWinFile(ft100ns, pFileTime);
}

// FILETIME �\���̂� 100ns �P�ʂ� uitn64_t �l�ɕϊ�
FILETIME_100NS_T WinFileTimeToWinFileTime100ns(const FILETIME& ft)
{
	//return ((PLARGE_INTEGER)&ft)->QuadPart;

	ULARGE_INTEGER ularge{};

	ularge.LowPart = ft.dwLowDateTime;				// ��ʂ� 32 �r�b�g
	ularge.HighPart = ft.dwHighDateTime;			// ���ʂ� 32 �r�b�g

	return ularge.QuadPart;
}

void WinFileTime100nsToWinFile(FILETIME_100NS_T ft100ns, FILETIME* pFileTime)
{
	//((PLARGE_INTEGER)pFileTime)->QuadPart = ft100ns;

	ULARGE_INTEGER ularge{};
	ularge.QuadPart = static_cast<ULONGLONG>(ft100ns);

	pFileTime->dwLowDateTime = ularge.LowPart;		// ���32�r�b�g
	pFileTime->dwHighDateTime = ularge.HighPart;	// ����32�r�b�g
}

// �~���b����100�i�m�b�P�ʂւ̕ϊ�
#define HUNDRED_NANOSECONDS_PER_MILLISECOND		(10000ULL)

// 1601�N1��1������1970�N1��1���܂ł̃I�t�Z�b�g (�~���b)
#define EPOCH_DIFFERENCE						(11644473600000ULL)

// 1601�N1��1������1970�N1��1���܂ł̃I�t�Z�b�g (100�i�m�b�P��)
#define EPOCH_DIFFERENCE_100NS					(116444736000000000ULL)

// UTC �̃~���b�� Windows �̃t�@�C�������ɕϊ�
FILETIME_100NS_T UtcMillisToWinFileTime100ns(UTC_MILLIS_T argUtcMillis)
{
	return (argUtcMillis + EPOCH_DIFFERENCE) * HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows �̃t�@�C�������� UTC �̃~���b �ɕϊ�
UTC_MILLIS_T WinFileTime100nsToUtcMillis(FILETIME_100NS_T ft100ns)
{
	return (ft100ns - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows �̃t�@�C�������� UTC �̃~���b�ɕϊ�
UTC_MILLIS_T WinFileTimeToUtcMillis(const FILETIME& ft)
{
	// ���������A�~���b�P�ʂɕϊ�
	return (WinFileTimeToWinFileTime100ns(ft) - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows �̃t�@�C�����������[�J�����ԕ�����ɕϊ�
std::wstring WinFileTimeToLocalTimeStringW(const FILETIME& ft)
{
	return UtcMillisToLocalTimeStringW(WinFileTimeToUtcMillis(ft));
}

} // CSELIB

// EOF