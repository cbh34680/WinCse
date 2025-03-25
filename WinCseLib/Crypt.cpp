#include "WinCseLib.h"
#include <bcrypt.h>

namespace WinCseLib {

// AES を使って復号化
bool DecryptAES(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, 
    const std::vector<BYTE>& encrypted, std::vector<BYTE>* pDecrypted)
{
    LastErrorBackup _backup;

    APP_ASSERT(pDecrypted);

    bool ret = false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD cbKeyObject = 0, cbData = 0;
    std::vector<BYTE> keyObject, decrypted(encrypted.size());

    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    // AES アルゴリズムを開く
    ntstatus = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);

    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    // キーオブジェクトのサイズを取得
    ntstatus = ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbKeyObject,
        sizeof(DWORD), &cbData, 0);

    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    keyObject.resize(cbKeyObject);

    // キーを作成
    ntstatus = ::BCryptGenerateSymmetricKey(hAlg, &hKey, keyObject.data(), cbKeyObject,
        (PUCHAR)key.data(), (ULONG)key.size(), 0);

    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    // 復号処理
    ntstatus = ::BCryptDecrypt(hKey, (PUCHAR)encrypted.data(), (ULONG)encrypted.size(),
        NULL, (PUCHAR)iv.data(), (ULONG)iv.size(), decrypted.data(), (ULONG)decrypted.size(), &cbData, 0);

    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    // 実際のデータサイズに調整
    decrypted.resize(cbData);

    *pDecrypted = std::move(decrypted);

    ret = true;

exit:
    // キーとアルゴリズムをクリーンアップ
    if (hKey)
    {
        BCryptDestroyKey(hKey);
    }

    if (hAlg)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    return ret;
}

bool GetCryptKeyFromRegistry(std::string* pKeyStr)
{
    LastErrorBackup _backup;

    bool ret = false;

    HKEY hKey = NULL;
    LONG result = STATUS_UNSUCCESSFUL;
    DWORD dataType = 0;
    DWORD dataSize = 0;
    BYTE data[BUFSIZ] = {}; // データのバッファ

    // レジストリキーを開く
    result = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey);
    if (!NT_SUCCESS(result))
    {
        goto exit;
    }

    // 値のデータサイズを取得
    result = ::RegQueryValueExW(hKey, L"MachineGuid", NULL, &dataType, NULL, &dataSize);
    if (!NT_SUCCESS(result))
    {
        goto exit;
    }

    if (dataSize > sizeof(data))
    {
        goto exit;
    }
    if (dataType != REG_SZ)
    {
        goto exit;
    }

    // 値のデータを取得
    result = ::RegQueryValueExW(hKey, L"MachineGuid", NULL, &dataType, data, &dataSize);
    if (!NT_SUCCESS(result))
    {
        goto exit;
    }

    if (dataSize > sizeof(data))
    {
        goto exit;
    }

    {
        // "1111-2222-3333" から "-" を削除
        std::wstring str{ (wchar_t*)data };
        str.erase(std::remove(str.begin(), str.end(), L'-'), str.end());

        *pKeyStr = WC2MB(str);
    }

    ret = true;

exit:
    if (hKey)
    {
        RegCloseKey(hKey);
    }

    return ret;
}

} // namespace WinCseLib

// EOF