#include "WinCseLib.h"
#include <iomanip>


namespace WCSE {

// UTC �~���b�� YYYY-MM-DD HH:MI:SS.NNN ������ɕϊ�
std::wstring UtcMilliToLocalTimeStringW(UINT64 milliseconds)
{
	// �~���b�� chrono::milliseconds �ɕϊ�
	const std::chrono::milliseconds ms{ milliseconds };

	// �~���b���� chrono::system_clock::time_point �ɕϊ�
	const auto tp{ std::chrono::system_clock::time_point(ms) };

	// time_point �� std::time_t �ɕϊ�
	const auto time = std::chrono::system_clock::to_time_t(tp);

	// �~���b�������擾
	const int fractional_seconds = milliseconds % 1000;

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
std::wstring WinFileTime100nsToLocalTimeStringW(UINT64 ft100ns)
{
	return UtcMilliToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns));
}

std::string WinFileTime100nsToLocalTimeStringA(UINT64 ft100ns)
{
	return WC2MB(UtcMilliToLocalTimeStringW(WinFileTime100nsToUtcMillis(ft100ns)));
}

// time_point �� UTC �̃~���b�ɕϊ�
long long int TimePointToUtcMillis(const std::chrono::system_clock::time_point& tp)
{
	// �G�|�b�N����̌o�ߎ��Ԃ��~���b�P�ʂŎ擾
	const auto duration{ tp.time_since_epoch() };

	// �b�P�ʂ�Unix���Ԃɕϊ�
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// time_point �𕶎���ɕϊ�
std::wstring TimePointToLocalTimeStringW(const std::chrono::system_clock::time_point& tp)
{
	return UtcMilliToLocalTimeStringW(TimePointToUtcMillis(tp));
}

// ���݂̎����� UTC �̃~���b�Ŏ擾
UINT64 GetCurrentUtcMillis()
{
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);

	return WinFileTimeToUtcMillis(ft);
}

UINT64 GetCurrentWinFileTime100ns()
{
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);

	return WinFileTimeToWinFileTime100ns(ft);
}

UINT64 STCTimeToUTCMilliSecW(const std::wstring& path)
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

UINT64 STCTimeToUTCMilliSecA(const std::string& path)
{
	return STCTimeToUTCMilliSecW(MB2WC(path));
}

UINT64 STCTimeToWinFileTimeW(const std::wstring& path)
{
	return UtcMillisToWinFileTime100ns(STCTimeToUTCMilliSecW(path));
}

// UTC �̃~���b�� FILETIME �\���̂ɕϊ�
void UtcMillisToWinFileTime(UINT64 argUtcMillis, FILETIME* pFileTime)
{
	APP_ASSERT(pFileTime);

	const auto ft100ns = UtcMillisToWinFileTime100ns(argUtcMillis);

#if 0
	// FILETIME�\���̂ɕϊ�
	pFileTime->dwLowDateTime = (DWORD)(ft100ns & 0xFFFFFFFF);
	pFileTime->dwHighDateTime = (DWORD)(ft100ns >> 32);
#else
	WinFileTime100nsToWinFile(ft100ns, pFileTime);
#endif
}

// FILETIME �\���̂� 100ns �P�ʂ� uitn64_t �l�ɕϊ�
UINT64 WinFileTimeToWinFileTime100ns(const FILETIME& ft)
{
	return ((PLARGE_INTEGER)&ft)->QuadPart;
}

void WinFileTime100nsToWinFile(UINT64 ft100ns, FILETIME* pFileTime)
{
	((PLARGE_INTEGER)pFileTime)->QuadPart = ft100ns;
}

// �~���b����100�i�m�b�P�ʂւ̕ϊ�
#define HUNDRED_NANOSECONDS_PER_MILLISECOND		(10000ULL)

// 1601�N1��1������1970�N1��1���܂ł̃I�t�Z�b�g (�~���b)
#define EPOCH_DIFFERENCE						(11644473600000ULL)

// 1601�N1��1������1970�N1��1���܂ł̃I�t�Z�b�g (100�i�m�b�P��)
#define EPOCH_DIFFERENCE_100NS					(116444736000000000ULL)

// UTC �̃~���b�� Windows �̃t�@�C�������ɕϊ�
UINT64 UtcMillisToWinFileTime100ns(UINT64 argUtcMillis)
{
	return (argUtcMillis + EPOCH_DIFFERENCE) * HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows �̃t�@�C�������� UTC �̃~���b �ɕϊ�
UINT64 WinFileTime100nsToUtcMillis(UINT64 ft100ns)
{
	return (ft100ns - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows �̃t�@�C�������� UTC �̃~���b�ɕϊ�
UINT64 WinFileTimeToUtcMillis(const FILETIME &ft)
{
	// ���������A�~���b�P�ʂɕϊ�
	return (WinFileTimeToWinFileTime100ns(ft) - EPOCH_DIFFERENCE_100NS) / HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// Windows �̃t�@�C�����������[�J�����ԕ�����ɕϊ�
std::wstring WinFileTimeToLocalTimeStringW(const FILETIME &ft)
{
	return UtcMilliToLocalTimeStringW(WinFileTimeToUtcMillis(ft));
}

} // WCSE

// EOF