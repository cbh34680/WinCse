#include "WinCseLib.h"
#include <bcrypt.h>

namespace WinCseLib {

// AES ���g���ĕ�����
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
    BYTE data[BUFSIZ] = {}; // �f�[�^�̃o�b�t�@

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