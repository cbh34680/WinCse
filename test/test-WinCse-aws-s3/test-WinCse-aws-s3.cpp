#include <iostream>
#include <Windows.h>
#include "AwsS3.hpp"

using namespace WinCseLib;

void test4()
{
    std::wstring parentDir;
    std::wstring filename;

    const wchar_t* keys[] =
    {
        L"",
        L"dir",
        L"dir/",
        L"dir/key.txt",
        L"dir/key.txt/",
        L"dir/subdir/key.txt",
        L"dir/subdir/key.txt/"
    };

    for (const auto key: keys)
    {
        bool b = WinCseLib::SplitPath(key, &parentDir, &filename);

        wprintf(L"key=[%s] b=[%s] dir=[%s] file=[%s]\n",
            key, BOOL_CSTRW(b), parentDir.c_str(), filename.c_str());
    }
}

int wmain(int argc, wchar_t** argv)
{
    int test1(int argc, wchar_t** argv);
	//test1(argc, argv);

	//test2();
    //test3();
    //test4();

    int test5(int argc, wchar_t** argv);
    test5(argc, argv);

    return 0;
}

// EOF