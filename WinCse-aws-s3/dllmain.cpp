// dllmain.cpp : DLL アプリケーションのエントリ ポイントを定義します。

//#pragma comment(lib, "winfsp-x64.lib")
// winfsp-$(PlatformTarget).dll
#pragma comment(lib, "aws-cpp-sdk-core.lib")
#pragma comment(lib, "aws-cpp-sdk-s3.lib")
#pragma comment(lib, "WinCseLib.lib")

#pragma warning(disable: 4100)

#include "Windows.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

