#include "WinCseLib.h"
#include <bcrypt.h>
#include <sstream>
#include <iomanip>

//
// ��� ChatGPT �ɂ�萶��
//

namespace WCSE {

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

    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    // AES �A���S���Y�����J��

    ntstatus = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);

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

bool GetCryptKeyFromRegistryA(std::string* pOutput)
{
    std::wstring output;

    if (GetCryptKeyFromRegistryW(&output))
    {
        *pOutput = WC2MB(output);

        return true;
    }

    return false;
}

bool GetCryptKeyFromRegistryW(std::wstring* pOutput)
{
    bool ret = false;

    HKEY hKey = NULL;
    LONG result = STATUS_UNSUCCESSFUL;
    DWORD dataType = 0;
    DWORD dataSize = 0;
    BYTE data[BUFSIZ] = {};     // �f�[�^�̃o�b�t�@

    // ���W�X�g���L�[���J��

    result = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey);
    if (!NT_SUCCESS(result))
    {
        goto exit;
    }

    // �l�̃f�[�^�T�C�Y���擾

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

    // �l�̃f�[�^���擾
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
        // "1111-2222-3333" ���� "-" ���폜

        std::wstring output{ (wchar_t*)data };

        output.erase(std::remove(output.begin(), output.end(), L'-'), output.end());

        *pOutput = output;
    }

    ret = true;

exit:
    if (hKey)
    {
        ::RegCloseKey(hKey);
    }

    return ret;
}

static std::string BytesToHex(const std::vector<BYTE>& bytes)
{
    std::ostringstream oss;

    for (BYTE byte : bytes)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
    }

    return oss.str();
}

bool ComputeSHA256W(const std::wstring& input, std::wstring* pOutput)
{
    std::string output;

    if (ComputeSHA256A(WC2MB(input), &output))
    {
        *pOutput = MB2WC(output);
        return true;
    }

    return false;
}

bool ComputeSHA256A(const std::string& input, std::string* pOutput)
{
    bool ret = false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD hashObjectSize = 0;
    DWORD dataSize = 0;

    std::vector<BYTE> hashObject;
    std::vector<BYTE> hashValue(32);        // SHA-256 �̃n�b�V���T�C�Y�� 32 �o�C�g

    std::string output;

    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    ntstatus = ::BCryptOpenAlgorithmProvider(&hAlg,
        BCRYPT_SHA256_ALGORITHM, nullptr, 0);
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

    ntstatus = ::BCryptFinishHash(hHash, hashValue.data(), (ULONG)hashValue.size(), 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    *pOutput = BytesToHex(hashValue);

    ret = true;

exit:
    if (hHash)
    {
        ::BCryptDestroyHash(hHash);
        hHash = nullptr;
    }

    if (hAlg)
    {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        hAlg = nullptr;
    }

    return ret;
}

} // namespace WCSE

// EOF