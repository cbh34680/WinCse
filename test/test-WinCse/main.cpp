#pragma comment(lib, "Crypt32.lib")             // CryptBinaryToStringA

#pragma comment(lib, "WinCseLib.lib")
#pragma comment(lib, "WinCse-aws-s3.lib")

#include <Windows.h>
#include <iostream>
#include <optional>

static void setup_stdout();

// [CPP/CPP-File.cpp]
void t_CPP_File();
void t_CPP_File_Win32();

// [CPP/CPP-Misc.cpp]
void t_CPP_Misc();

// [WinCseLib/CSELIB-System.cpp]
void t_WinCseLib_System_AbnormalEnd();

// [WinCseLib/CSELIB-Crypt.cpp]
void t_WinCseLib_Crypt();

// [WinCseLib/CSELIB-File.cpp]
void t_WinCseLib_File();

// [WinCseLib/CSELIB-String.cpp]
void t_WinCseLib_String_MBWC();
void t_WinCseLib_String_TrimW();
void t_WinCseLib_String_SplitString();
void t_WinCseLib_String_SplitPath();

// [WinCseLib/CSELIB-ObjectKey.cpp]
void t_WinCseLib_ObjectKey_fromPath();
void t_WinCseLib_ObjectKey_fromWinPath();
void t_WinCseLib_ObjectKey();

// [WinCseLib/CSELIB-Protect.cpp]
void t_WinCseLib_Protect();

// [WinCseLib/CSELIB-Time.cpp]
void t_WinCseLib_Time();

// [WinCseLib-aws-s3/CSEAS3-Find.cpp]
void t_WinCseLib_aws_s3_Find();


int wmain(int, wchar_t**)
{
	setup_stdout();

    WCHAR curdir[MAX_PATH];
    ::GetCurrentDirectoryW(_countof(curdir), curdir);

    std::wcout << L"Current Directory=" << curdir << std::endl;

#if 0
    /* [CPP/CPP-File.cpp] */
    t_CPP_File();
    t_CPP_File_Win32();
#endif

#if 1
    /* [CPP/CPP-Misc.cpp] */
    t_CPP_Misc();
#endif

#if 0
	/* [WinCseLib/CSELIB-System.cpp] */
    t_WinCseLib_System_AbnormalEnd();
#endif

#if 0
    /* [WinCseLib/CSELIB-Crypt.cpp] */
    t_WinCseLib_Crypt();
#endif

#if 0
    /* [WinCseLib/CSELIB-File.cpp] */
    t_WinCseLib_File();
#endif

#if 0
    /* [WinCseLib/CSELIB-String.cpp */
    t_WinCseLib_String_MBWC();
    t_WinCseLib_String_TrimW();
    t_WinCseLib_String_SplitString();
    t_WinCseLib_String_SplitPath();
#endif

#if 0
    /* [WinCseLib/CSELIB-ObjectKey.cpp */
    t_WinCseLib_ObjectKey();
    t_WinCseLib_ObjectKey_fromPath();
    t_WinCseLib_ObjectKey_fromWinPath();
#endif

#if 0
    /* [WinCseLib/CSELIB-Protect.cpp] */
    t_WinCseLib_Protect();
#endif

#if 0
    /* [WinCseLib/CSELIB-Time.cpp] */
    t_WinCseLib_Time();
#endif

#if 0
    /* [WinCseLib-aws-s3/CSEAS3-Find.cpp] */
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

    // ‚±‚ê‚â‚ç‚È‚¢‚Æ“ú–{Œê‚ªo—Í‚Å‚«‚È‚¢
    _wsetlocale(LC_ALL, L"");
    //setlocale(LC_ALL, "");

    std::locale::global(std::locale("", std::locale::ctype));
    std::cout.imbue(std::locale("", std::locale::ctype));
    std::wcout.imbue(std::locale("", std::locale::ctype));

    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
}


// EOF