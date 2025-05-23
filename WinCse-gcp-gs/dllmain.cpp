// dllmain.cpp : DLL アプリケーションのエントリ ポイントを定義します。
#include <Windows.h>

#pragma comment(lib, "google_cloud_cpp_storage.lib")
#pragma comment(lib, "google_cloud_cpp_rest_internal.lib")
#pragma comment(lib, "google_cloud_cpp_common.lib")
#pragma comment(lib, "abseil_dll.lib")

#ifdef _DEBUG
#pragma comment(lib, "libcurl-d.lib")
#else
#pragma comment(lib, "libcurl.lib")
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "crc32c.lib")
#pragma comment(lib, "bcrypt.lib")

#pragma comment(lib, "WinCseLib.lib")
#pragma comment(lib, "WinCseDevice.lib")

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

