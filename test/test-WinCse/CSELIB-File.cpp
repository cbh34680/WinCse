#include "WinCseLib.h"
#include <iostream>

using namespace CSELIB;

void t_WinCseLib_File()
{
	std::wcout << L"[directries]" << std::endl;

	forEachDirs("..\\", [](const auto& wfd, const auto& fullPath)
	{
		std::wcout << wfd.cFileName << std::endl;
		std::wcout << fullPath << std::endl;
	});

	std::wcout << L"[files]" << std::endl;

	forEachFiles("..\\", [](const auto& wfd, const auto& fullPath)
	{
		std::wcout << wfd.cFileName << std::endl;
		std::wcout << fullPath << std::endl;
	});

	std::wcout << L"done." << std::endl;
}

// EOF