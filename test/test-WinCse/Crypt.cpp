#include "WinCseLib.h"
#include <iostream>

using namespace CSELIB;

void t_WinCseLib_Crypt()
{
    // レジストリ "HKLM:\SOFTWARE\Microsoft\Cryptography" から "MachineGuid" の値を取得
    std::string secureKeyStr;
    GetCryptKeyFromRegistryA(&secureKeyStr);

    // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する
    std::vector<BYTE> aesKey{ secureKeyStr.begin(), secureKeyStr.end() };
    //std::vector<BYTE> aesIV{ secureKeyStr.begin(), secureKeyStr.begin() + 16 };

    // test-encrypt-str.ps1 で作成した DATA の BASE64 文字列
    std::string concatB64Str{ "60sNtN3sCNXh2uKRWFAK5M2KiQYxNNO0N/JZRHSL20Y=" };

    // DATA の BASE64 文字列をデコード
    std::string concatStr;
    BOOL b = Base64DecodeA(concatB64Str, &concatStr);
    APP_ASSERT(b);

    std::vector<BYTE> concatBytes{ concatStr.begin(), concatStr.end() };

    // 先頭の 16 byte が IV
    std::vector<BYTE> aesIV{ concatBytes.begin(), concatBytes.begin() + 16 };

    // それ以降がデータ
    std::vector<BYTE> data{ concatBytes.begin() + 16, concatBytes.end() };


    // 復号化
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