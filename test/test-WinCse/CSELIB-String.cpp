#include "WinCseLib.h"
#include <iostream>

using namespace CSELIB;

void t_WinCseLib_String_MBWC()
{
	const auto fileW1 = __FILEW__;
	const auto fileA1 = WC2MB(fileW1);
	const auto fileW2 = MB2WC(fileA1);

	std::wcout << L"fileW1=" << fileW1 << std::endl;
	std::cout  <<  "fileA1=" << fileA1 << std::endl;
	std::wcout << L"fileW2=" << fileW2 << std::endl;

	/*
	const auto jpW1 = L"“ú–{Œê";
	const auto jpA1 = WC2MB(jpW1);
	const auto jpW2 = MB2WC(jpA1);

	std::wcout << L"jpW1=" << jpW1 << std::endl;
	std::cout  <<  "jpA1=" << jpA1 << std::endl;
	std::wcout << L"jpW2=" << jpW2 << std::endl;
	*/
}

void t_WinCseLib_String_TrimW()
{
	std::wcout << TrimW(L" trim string")			<< L'|' << std::endl;
	std::wcout << TrimW(L"trim string ")			<< L'|' << std::endl;
	std::wcout << TrimW(L" trim string ")			<< L'|' << std::endl;
	std::wcout << TrimW(L"    trim string    ")	<< L'|' << std::endl;
}

template <typename ContainerT>
static void printWStrs(const ContainerT& strs)
{
	int i=0;
	for (const auto& str: strs)
	{
		std::wcout << L"[" << i << L"] '" << str << L"'" << std::endl;
		i++;
	}
}

void t_WinCseLib_String_SplitString()
{
	printWStrs(SplitString(L" split string ", L' ', true));
	printWStrs(SplitString(L"   split   string   ", L' ', true));
	printWStrs(SplitString(L" split string ", L' ', false));
	printWStrs(SplitString(L"   split   string   ", L' ', false));
}

void t_WinCseLib_String_SplitPath()
{
	std::wstring parentDir;
	std::wstring filename;

	PCWSTR keys[] =
	{
		L"",
		L"dir",
		L"dir/",
		L"dir/key.txt",
		L"dir/key.txt/",
		L"dir/subdir/key.txt",
		L"dir/subdir/key.txt/",

		L"  ",
		L"dir",
		L"dir//",
		L"dir// key.txt",
		L"dir//  key.txt//",
		L"dir// subdir //key.txt",
		L"dir  //  subdir  // key.txt//",

		L"/dir/subdir/key.txt",
	};

	for (const auto key: keys)
	{
		bool b = SplitObjectKey(key, &parentDir, &filename);

		wprintf(L"%s: input='%s' dir=[%s] file=[%s]\n",
			BOOL_CSTRW(b), key, parentDir.c_str(), filename.c_str());
	}
}


// EOF