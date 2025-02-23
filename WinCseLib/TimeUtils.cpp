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

	// time_t �� time_point �ɕϊ�
	const auto time_point = std::chrono::system_clock::from_time_t(st.st_ctime);

	// �G�|�b�N����̌o�ߎ��Ԃ��~���b�P�ʂŎ擾
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

// UTC �̃~���b�� Windows �̃t�@�C�������ɕϊ�
uint64_t UtcMillisToWinFileTimeIn100ns(uint64_t utcMilliseconds)
{
	// 1601�N1��1������̃I�t�Z�b�g
	static const uint64_t EPOCH_DIFFERENCE = 11644473600000LL; // �~���b

	// �~���b��100�i�m�b�P�ʂɕϊ����A�I�t�Z�b�g�����Z
	static const uint64_t HUNDRED_NANOSECONDS_PER_MILLISECOND = 10000;

	return (utcMilliseconds + EPOCH_DIFFERENCE) * HUNDRED_NANOSECONDS_PER_MILLISECOND;
}

// FILETIME �\���̂� 100ns �P�ʂ� uitn64_t �l�ɕϊ�
uint64_t WinFileTimeIn100ns(const FILETIME& ft)
{
	ULARGE_INTEGER ull{};

	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;

	return ull.QuadPart;
}

// UTC �̃~���b�� FILETIME �\���̂ɕϊ�
void UtcMillisToWinFileTime(uint64_t utcMilliseconds, FILETIME* ft)
{
	APP_ASSERT(ft);

	const auto fileTime = UtcMillisToWinFileTimeIn100ns(utcMilliseconds);

	// FILETIME�\���̂ɕϊ�
	ft->dwLowDateTime = (DWORD)(fileTime & 0xFFFFFFFF);
	ft->dwHighDateTime = (DWORD)(fileTime >> 32);
}

// Windows �̃t�@�C�������� UTC �̃~���b�ɕϊ�
uint64_t WinFileTimeToUtcMillis(const FILETIME &ft)
{
	// FILETIME �� 64 �r�b�g�̐����ɕϊ�
	ULARGE_INTEGER ull = {};

	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;

	// 1601 �N 1 �� 1 ������̌o�ߎ��Ԃ� 100 �i�m�b�P�ʂŕێ����Ă��邽�߁A1970 �N 1 �� 1 ���Ƃ̍����v�Z
	static const uint64_t UNIX_EPOCH_DIFFERENCE = 116444736000000000ULL;

	// ���������A�~���b�P�ʂɕϊ�
	return (ull.QuadPart - UNIX_EPOCH_DIFFERENCE) / 10000ULL;
}

// time_point �� UTC �̕b�ɕϊ�
long long int TimePointToUtcSecs(const std::chrono::system_clock::time_point& tp)
{
	// �G�|�b�N����̌o�ߎ��Ԃ��~���b�P�ʂŎ擾
	auto duration = tp.time_since_epoch();

	// �~���b�P�ʂ�Unix���Ԃɕϊ�
	//auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

	// �b�P�ʂ�Unix���Ԃɕϊ�
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

	return seconds;
}

// �t�@�C���n���h������ FILETIME �̒l���擾
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

// ���݂̎����� UTC �̃~���b�Ŏ擾
uint64_t GetCurrentUtcMillis()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);

	return WinFileTimeToUtcMillis(ft);
}

} // WinCseLib

// EOF