// test-WinCseLib.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#pragma comment(lib, "WinCseLib.lib")

#include "WinCseLib.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <bcrypt.h>

using namespace WinCseLib;
using namespace std;

void test1()
{
    wcout << MB2WC("abc 012") << endl;
    cout << WC2MB(L"abc 012") << endl;

    cout << Base64EncodeA("C:\\Windows\\system32\\drivers\\hosts") << endl;
    cout << Base64DecodeA("QzpcV2luZG93c1xzeXN0ZW0zMlxkcml2ZXJzXGhvc3Rz") << endl;

    cout << URLEncodeA("https://company.com/s /p+/m-/u_/d$") << endl;
    cout << URLDecodeA("https%3A%2F%2Fcompany.com%2Fs%20%2Fp%2B%2Fm-%2Fu_%2Fd%24") << endl;

    wcout << TrimW(L" \t [a][\t][ ][b]\t \t") << std::endl;


    wcout << "DONE" << endl;
}

mutex lock_map;
map<string, shared_ptr<mutex>> mutex_map;

shared_ptr<mutex> getlock(const string& id)
{
    lock_guard<mutex> a(lock_map);
    auto it = mutex_map.find(id);
    return it->second;
}

void test2_worker1(const string& id)
{
    lock_guard<mutex> g(*getlock(id));

    cout << __FUNCTION__ << ":enter:" << id << endl;
    ::Sleep(5000);
    cout << __FUNCTION__ << ":leave:" << id << endl;
}

void test2_worker2(const string& id)
{
    lock_guard<mutex> g(*getlock(id));

    cout << __FUNCTION__ << ":enter:" << id << endl;
    ::Sleep(5000);
    cout << __FUNCTION__ << ":leave:" << id << endl;
}

void test2_worker3(const string& id)
{
    lock_guard<mutex> g(*getlock(id));

    cout << __FUNCTION__ << ":enter:" << id << endl;
    cout << __FUNCTION__ << ":leave:" << id << endl;
}

void test2()
{
    mutex_map.emplace("001", make_shared<mutex>());
    mutex_map.emplace("100", make_shared<mutex>());

    thread t1{ test2_worker1, "001" };
    thread t2{ test2_worker2, "001" };
    thread t3{ test2_worker3, "100" };

    t1.join();
    t2.join();
    t3.join();
}

void test3()
{
    // レジストリ "HKLM:\SOFTWARE\Microsoft\Cryptography" から "MachineGuid" の値を取得
    std::string secureKeyStr;
    GetCryptKeyFromRegistry(&secureKeyStr);

    // MachineGuid の値を AES の key とし、iv には key[0..16] を設定する
    std::vector<BYTE> aesKey{ secureKeyStr.begin(), secureKeyStr.end() };
    std::vector<BYTE> aesIV{ secureKeyStr.begin(), secureKeyStr.begin() + 16 };

    // test-encrypt-str.ps1 で作成した BASE64 文字列
    std::string encryptedB64Str{ "lnsb3SErHgurhMvgiYaROQ==" };

    // BASE64 文字列をデコード
    std::string encryptedStr = Base64DecodeA(encryptedB64Str);
    std::vector<BYTE> encrypted{ encryptedStr.begin(), encryptedStr.end() };

    // 復号化
    std::vector<unsigned char> decrypted;
    if (DecryptAES(aesKey, aesIV, encrypted, &decrypted))
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

int main()
{
    ::SetConsoleOutputCP(CP_UTF8);
    _wsetlocale(LC_ALL, L"");
    setlocale(LC_ALL, "");
    wcout.imbue(locale(""));
    wcerr.imbue(locale(""));
    cout.imbue(locale(""));
    cerr.imbue(locale(""));

    //test1();
    //test2();
    test3();

    return EXIT_SUCCESS;
}

// EOF