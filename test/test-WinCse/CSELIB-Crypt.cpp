#include "WinCseLib.h"
#include <iostream>

using namespace CSELIB;

void t_WinCseLib_Crypt()
{
    // ���W�X�g�� "HKLM:\SOFTWARE\Microsoft\Cryptography" ���� "MachineGuid" �̒l���擾
    std::string secureKeyStr;
    GetCryptKeyFromRegistryA(&secureKeyStr);

    // MachineGuid �̒l�� AES �� key �Ƃ��Aiv �ɂ� key[0..16] ��ݒ肷��
    std::vector<BYTE> aesKey{ secureKeyStr.begin(), secureKeyStr.end() };
    //std::vector<BYTE> aesIV{ secureKeyStr.begin(), secureKeyStr.begin() + 16 };

    // test-encrypt-str.ps1 �ō쐬���� DATA �� BASE64 ������
    std::string concatB64Str{ "60sNtN3sCNXh2uKRWFAK5M2KiQYxNNO0N/JZRHSL20Y=" };

    // DATA �� BASE64 ��������f�R�[�h
    std::string concatStr;
    BOOL b = Base64DecodeA(concatB64Str, &concatStr);
    APP_ASSERT(b);

    std::vector<BYTE> concatBytes{ concatStr.begin(), concatStr.end() };

    // �擪�� 16 byte �� IV
    std::vector<BYTE> aesIV{ concatBytes.begin(), concatBytes.begin() + 16 };

    // ����ȍ~���f�[�^
    std::vector<BYTE> data{ concatBytes.begin() + 16, concatBytes.end() };


    // ������
    std::vector<unsigned char> decrypted;
    if (DecryptAES(aesKey, aesIV, data, &decrypted))
    {
        const std::string str{ (char*)decrypted.data() };

        if (str == "Hello, World!")
        {
            std::cout << "Decryption successful: [" << str << "] len=" << str.length() << std::endl;
        }
        else
        {
            std::cout << "un-expected string: " << str << std::endl;
        }
    }
    else
    {
        std::cerr << "Decryption failed\n";
    }
}

// EOF