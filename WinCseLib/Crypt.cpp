#include "WinCseLib.h"
#include <bcrypt.h>
#include <iomanip>

//
// ��� ChatGPT �ɂ�萶��
//

namespace WCSE {

// GUID �̐���
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

// AES ���g���ĕ�����
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

    // AES �A���S���Y�����J��

    auto ntstatus = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    // �L�[�I�u�W�F�N�g�̃T�C�Y���擾

    ntstatus = ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbKeyObject,
        sizeof(DWORD), &cbData, 0);

    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    keyObject.resize(cbKeyObject);

    // �L�[���쐬

    ntstatus = ::BCryptGenerateSymmetricKey(hAlg, &hKey, keyObject.data(), cbKeyObject,
        (PUCHAR)key.data(), (ULONG)key.size(), 0);

    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    // ��������

    ntstatus = ::BCryptDecrypt(hKey, (PUCHAR)encrypted.data(), (ULONG)encrypted.size(),
        NULL, (PUCHAR)iv.data(), (ULONG)iv.size(), decrypted.data(), (ULONG)decrypted.size(), &cbData, 0);

    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    // ���ۂ̃f�[�^�T�C�Y�ɒ���

    decrypted.resize(cbData);

    *pDecrypted = std::move(decrypted);

    ret = true;

exit:
    // �L�[�ƃA���S���Y�����N���[���A�b�v
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
    BYTE data[BUFSIZ];     // �f�[�^�̃o�b�t�@

    // ���W�X�g���L�[���J��

    lstatus = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey);
    if (lstatus != ERROR_SUCCESS)
    {
        goto exit;
    }

    // �l�̃f�[�^�T�C�Y���擾

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

    // �l�̃f�[�^���擾
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
        // "1111-2222-3333" ���� "-" ���폜

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
    //std::vector<BYTE> hashValue(32);        // SHA-256 �̃n�b�V���T�C�Y�� 32 �o�C�g
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

} // namespace WCSE

// EOF