#include "WinCseLib.h"
#include <bcrypt.h>
#include <iomanip>

//
// ����R�[�h�͎�� ChatGPT/Copilot �ɍ���Ă������
//

namespace CSELIB {

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

bool DecryptCredentialStringA(const std::string& argSecretKey, std::string* pInOut)
{
    NEW_LOG_BLOCK();
    APP_ASSERT(pInOut);

    const auto concatB64Str{ *pInOut };

    traceA("concatB64Str=%s***", SafeSubStringA(concatB64Str, 0, 5).c_str());

    // MachineGuid �̒l�� AES �� key �Ƃ��Aiv �ɂ� key[0..16] ��ݒ肷��

    // BASE64 ��������f�R�[�h

    std::string concatStr;
    if (!Base64DecodeA(concatB64Str, &concatStr))
    {
        errorW(L"fault: Base64DecodeA");
        return false;
    }

    const std::vector<BYTE> concatBytes{ concatStr.cbegin(), concatStr.cend() };

    if (concatBytes.size() < 17)
    {
        // IV + �f�[�^�Ȃ̂ōŒ�ł� 16 + 1 byte �͕K�v

        errorW(L"fault: concatBytes.size() < 17");
        return false;
    }

    // �擪�� 16 byte �� IV

    const std::vector<BYTE> aesIV{ concatStr.cbegin(), concatStr.cbegin() + 16 };

    // ����ȍ~���f�[�^

    const std::vector<BYTE> encrypted{ concatStr.cbegin() + 16, concatStr.cend() };

    // ������

    std::vector<BYTE> decrypted;

    const std::vector<BYTE> aesKey{ argSecretKey.cbegin(), argSecretKey.cend() };

    if (!DecryptAES(aesKey, aesIV, encrypted, &decrypted))
    {
        errorW(L"fault: DecryptAES");
        return false;
    }

    // ���ꂾ�� strlen() �̃T�C�Y�ƈ�v���Ȃ��Ȃ�
    //str.assign(decrypted.begin(), decrypted.end());

    // ���͂� '\0' �I�[�ł��邱�Ƃ�O��� char* ���� std::string ������������

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