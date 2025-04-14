// dllmain.cpp : DLL アプリケーションのエントリ ポイントを定義します。
#include <Windows.h>

#pragma comment(lib, "winfsp-x64.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "Rpcrt4.lib")

#pragma warning(disable: 4100)

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

