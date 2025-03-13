// util-ClearCacheRequest.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//

#include <iostream>
#include <Windows.h>

int wmain(int argc, wchar_t** argv)
{
	HANDLE hEvent = ::OpenEvent(EVENT_ALL_ACCESS, FALSE, L"Global\\WinCse-AwsS3-util-clear-cache");
	if (!hEvent)
	{
		const auto lerr = ::GetLastError();
		std::wcerr << L"fault: OpenEvent: err=" << lerr << std::endl;

		return EXIT_FAILURE;
	}

	if (!::SetEvent(hEvent))
	{
		const auto lerr = ::GetLastError();
		std::wcerr << L"fault: SetEvent: err=" << lerr << std::endl;

		return EXIT_FAILURE;
	}

	::CloseHandle(hEvent);

	return EXIT_SUCCESS;
}

// EOF