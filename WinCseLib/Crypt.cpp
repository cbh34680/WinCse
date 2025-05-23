#include "WinCseLib.h"
#include <bcrypt.h>
#include <iomanip>

//
// 難しいコードは主に ChatGPT/Copilot に作ってもらった
//

namespace CSELIB {

// GUID の生成
std::wstring CreateGuidW()
{
    GUID guid{};
    const auto hresult = ::CoCreateGuid(&guid);
    APP_ASSERT(hresult == S_OK);

    RPC_WSTR rpcString = nullptr;

    const auto rpcStatus = ::UuidToStringW(&guid, &rpcString);
    APP_ASSERT(rpcStatus == RPC_S_OK);

    const std::wstring guidString{ reinterpret_cast<wchar_t*>(rpcString) };
    ::RpcStringFreeW(&rpcString);

    return guidString;
}

// AES を使って復号化
bool DecryptAES(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, 
    const std::vector<BYTE>& encrypted, std::vector<BYTE>* pDecrypted)
{
    APP_ASSERT(pDecrypted);

    bool ret = false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD cbKeyObject = 0;
    DWORD cbData = 0;
    std::vector<BYTE> keyObject;
    std::vector<BYTE> decrypted(encrypted.size());

    // AES アルゴリズムを開く

    auto ntstatus = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
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
        ::BCryptDestroyKey(hKey);
    }

    if (hAlg)
    {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    return ret;
}

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

static std::string BytesToHex(const BYTE* bytes, const size_t bytesSize)
{
    std::ostringstream ss;

    for (int i=0; i<bytesSize; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }

    return ss.str();
}

NTSTATUS ComputeSHA256W(const std::wstring& input, std::wstring* pOutput)
{
    std::string output;

    const auto ntstatus = ComputeSHA256A(WC2MB(input), &output);
    if (NT_SUCCESS(ntstatus))
    {
        *pOutput = MB2WC(output);
    }

    return ntstatus;
}

NTSTATUS ComputeSHA256A(const std::string& input, std::string* pOutput)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD hashObjectSize = 0;
    DWORD dataSize = 0;

    std::vector<BYTE> hashObject;
    //std::vector<BYTE> hashValue(32);        // SHA-256 のハッシュサイズは 32 バイト
    BYTE hashValue[32];

    std::string output;

    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    ntstatus = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    ntstatus = ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
        (PBYTE)&hashObjectSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    hashObject.resize(hashObjectSize);

    ntstatus = ::BCryptCreateHash(hAlg, &hHash,
        hashObject.data(), hashObjectSize, nullptr, 0, 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    ntstatus = ::BCryptHashData(hHash, (PBYTE)input.data(), (ULONG)input.size(), 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    ntstatus = ::BCryptFinishHash(hHash, hashValue, (ULONG)sizeof(hashValue), 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    //*pOutput = BytesToHex(hashValue);
    *pOutput = BytesToHex(hashValue, sizeof(hashValue));

    ntstatus = STATUS_SUCCESS;

exit:
    if (hHash)
    {
        ::BCryptDestroyHash(hHash);
    }

    if (hAlg)
    {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    return ntstatus;
}

bool DecryptCredentialStringA(const std::string& argSecretKey, std::string* pInOut)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pInOut);

    const auto concatB64Str{ *pInOut };

    traceA("concatB64Str=%s***", SafeSubStringA(concatB64Str, 0, 5).c_str());

    // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する

    // BASE64 文字列をデコード

    std::string concatStr;
    if (!Base64DecodeA(concatB64Str, &concatStr))
    {
        errorW(L"fault: Base64DecodeA");
        return false;
    }

    const std::vector<BYTE> concatBytes{ concatStr.cbegin(), concatStr.cend() };

    if (concatBytes.size() < 17)
    {
        // IV + データなので最低でも 16 + 1 byte は必要

        errorW(L"fault: concatBytes.size() < 17");
        return false;
    }

    // 先頭の 16 byte が IV

    const std::vector<BYTE> aesIV{ concatStr.cbegin(), concatStr.cbegin() + 16 };

    // それ以降がデータ

    const std::vector<BYTE> encrypted{ concatStr.cbegin() + 16, concatStr.cend() };

    // 復号化

    std::vector<BYTE> decrypted;

    const std::vector<BYTE> aesKey{ argSecretKey.cbegin(), argSecretKey.cend() };

    if (!DecryptAES(aesKey, aesIV, encrypted, &decrypted))
    {
        errorW(L"fault: DecryptAES");
        return false;
    }

    // これだと strlen() のサイズと一致しなくなる
    //str.assign(decrypted.begin(), decrypted.end());

    // 入力が '\0' 終端であることを前提に char* から std::string を初期化する

    //str = (char*)decrypted.data();
    //*pInOut = std::move(str);

    const auto it = std::find_if(decrypted.cbegin(), decrypted.cend(), [](const auto ch)
    {
        return ch == '\0';
    });

    if (it == decrypted.cend())
    {
        errorW(L"fault: Not String size=%zu", decrypted.size());
        return false;
    }

    *pInOut = std::string{ (char*)decrypted.data() };

    traceA("success: DecryptAES result=%s***", SafeSubStringA(*pInOut, 0, 5).c_str());

    return true;
}

bool DecryptCredentialStringW(const std::wstring& argSecretKey, std::wstring* pInOut)
{
    const auto secretKey{ WC2MB(argSecretKey) };
    auto data{ WC2MB(*pInOut) };

    if (DecryptCredentialStringA(secretKey, &data))
    {
        *pInOut = MB2WC(data);

        return true;
    }

    return false;
}


} // namespace CSELIB

// EOF