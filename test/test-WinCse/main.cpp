#pragma comment(lib, "Crypt32.lib")             // CryptBinaryToStringA

#pragma comment(lib, "WinCseLib.lib")
#pragma comment(lib, "WinCse-aws-s3.lib")

#include <Windows.h>
#include <iostream>

static void setup_stdout();

// [WinCseLib/System.cpp]
void t_WinCseLib_System_AbnormalEnd();

// [WinCseLib/Crypt.cpp]
void t_WinCseLib_Crypt();

// [WinCseLib/String.cpp]
void t_WinCseLib_String_MBWC();
void t_WinCseLib_String_TrimW();
void t_WinCseLib_String_SplitString();
void t_WinCseLib_String_SplitPath();

// [WinCseLib/ObjectKey.cpp]
void t_WinCseLib_ObjectKey_fromPath();
void t_WinCseLib_ObjectKey_fromWinPath();
void t_WinCseLib_ObjectKey();

// [WinCseLib/Protect.cpp]
void t_WinCseLib_Protect();

// [WinCseLib/Time.cpp]
void t_WinCseLib_Time();

// [WinCseLib-aws-s3/Find.cpp]
void t_WinCseLib_aws_s3_Find();


int wmain(int, wchar_t**)
{
	setup_stdout();

#if 0
	/* [WinCseLib/System.cpp] */
    t_WinCseLib_System_AbnormalEnd();
#endif

#if 1
    /* [WinCseLib/Crypt.cpp] */
    t_WinCseLib_Crypt();
#endif

#if 0
    /* [WinCseLib/String.cpp */
    t_WinCseLib_String_MBWC();
    t_WinCseLib_String_TrimW();
    t_WinCseLib_String_SplitString();
    t_WinCseLib_String_SplitPath();
#endif

#if 0
    /* [WinCseLib/String.cpp */
    t_WinCseLib_ObjectKey();
    t_WinCseLib_ObjectKey_fromPath();
    t_WinCseLib_ObjectKey_fromWinPath();
#endif

#if 0
    /* [WinCseLib/Protect.cpp] */
    t_WinCseLib_Protect();
#endif

#if 0
    /* [WinCseLib/Time.cpp] */
    t_WinCseLib_Time();
#endif

#if 0
    /* [WinCseLib-aws-s3/Find.cpp] */
    t_WinCseLib_aws_s3_Find();
#endif

	return EXIT_SUCCESS;
}

static void setup_stdout()
{
    // chcp 65001
    //std::locale::global(std::locale("", LC_ALL));
    //std::wcout.imbue(std::locale("", LC_ALL));
    //std::wcerr.imbue(std::locale("", LC_ALL));

    // ������Ȃ��Ɠ��{�ꂪ�o�͂ł��Ȃ�
    _wsetlocale(LC_ALL, L"");
    //setlocale(LC_ALL, "");

    std::locale::global(std::locale("", std::locale::ctype));
    std::cout.imbue(std::locale("", std::locale::ctype));
    std::wcout.imbue(std::locale("", std::locale::ctype));

    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
}


// EOF