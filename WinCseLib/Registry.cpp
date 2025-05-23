#include "WinCseLib.h"

namespace CSELIB {

LSTATUS GetCryptKeyFromRegistryA(std::string* pOutput)
{
    std::wstring output;

    const auto lstatus = GetCryptKeyFromRegistryW(&output);
    if (lstatus == ERROR_SUCCESS)
    {
        *pOutput = WC2MB(output);
    }

    return lstatus;
}

LSTATUS GetCryptKeyFromRegistryW(std::wstring* pOutput)
{
    HKEY hKey = NULL;
    LSTATUS lstatus = ERROR_SUCCESS;
    DWORD dataType = 0;
    DWORD dataSize = 0;
    BYTE data[BUFSIZ];     // データのバッファ

    // レジストリキーを開く

    lstatus = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey);
    if (lstatus != ERROR_SUCCESS)
    {
        goto exit;
    }

    // 値のデータサイズを取得

    lstatus = ::RegQueryValueExW(hKey, L"MachineGuid", NULL, &dataType, NULL, &dataSize);
    if (lstatus != ERROR_SUCCESS)
    {
        goto exit;
    }

    if (dataSize > sizeof(data))
    {
        lstatus = ERROR_MORE_DATA;
        goto exit;
    }

    if (dataType != REG_SZ)
    {
        lstatus = ERROR_INVALID_DATATYPE;
        goto exit;
    }

    // 値のデータを取得
    lstatus = ::RegQueryValueExW(hKey, L"MachineGuid", NULL, &dataType, data, &dataSize);
    if (lstatus != ERROR_SUCCESS)
    {
        goto exit;
    }

    if (dataSize > sizeof(data))
    {
        lstatus = ERROR_MORE_DATA;
        goto exit;
    }

    data[dataSize] = '\0';

    {
        // "1111-2222-3333" から "-" を削除

        std::wstring output{ (wchar_t*)data };

        output.erase(std::remove(output.begin(), output.end(), L'-'), output.end());

        *pOutput = output;
    }

exit:
    if (hKey)
    {
        ::RegCloseKey(hKey);
    }

    return lstatus;
}

std::wstring GetMimeTypeFromFileName(const std::filesystem::path& argPath)
{
    HKEY hKey;
    std::wstring mimeType = L"application/octet-stream";

    auto extension{ argPath.extension().wstring() };

    if (::RegOpenKeyExW(HKEY_CLASSES_ROOT, extension.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        BYTE data[_MAX_EXT];
        DWORD dataSize = sizeof(data);

        if (::RegQueryValueExW(hKey, L"Content Type", nullptr, nullptr, data, &dataSize) == ERROR_SUCCESS)
        {
            if (dataSize < sizeof(data))
            {
                data[dataSize] = '\0';
                mimeType = (PCWSTR)data;
            }
        }

        ::RegCloseKey(hKey);
    }

    return mimeType;
}

}	// namespace CSELIB

// EOF