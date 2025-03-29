#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <winternl.h>
#pragma warning(push)
#pragma warning(disable:4005)           /* macro redefinition */
#include <ntstatus.h>
#pragma warning(pop)

#include <bcrypt.h>
#include <iostream>
#include <vector>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

std::string BytesToHex(const std::vector<BYTE>& bytes)
{
    std::ostringstream oss;

    for (BYTE byte : bytes)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
    }

    return oss.str();
}

bool ComputeSHA256(const std::string& input, std::string* pOutput)
{
    bool ret = false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD hashObjectSize = 0;
    DWORD dataSize = 0;

    std::vector<BYTE> hashObject;
    std::vector<BYTE> hashValue(32);        // SHA-256 のハッシュサイズは 32 バイト

    std::string output;

    NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

    ntstatus = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    ntstatus = ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjectSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(ntstatus))
    {
        goto exit;
    }

    hashObject.resize(hashObjectSize);

    ntstatus = ::BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0);
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

int test20()
{
    std::string input = "Hello, Win32!";
    std::string output;

    if (ComputeSHA256(input, &output))
    {
        std::cout << "SHA-256: " << output << std::endl;
    }


    return 0;
}
