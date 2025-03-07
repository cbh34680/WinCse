// test-WinCseLib.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#pragma comment(lib, "WinCseLib.lib")

#include "WinCseLib.h"
#include <iostream>
#include <thread>
#include <mutex>
//#include <map>
#include <unordered_map>
#include <regex>
#include <sstream>
#include <bcrypt.h>

using namespace WinCseLib;
using namespace std;

void test1()
{
    wcout << MB2WC("abc 012") << endl;
    cout << WC2MB(L"abc 012") << endl;

    wcout << L"JP[" << MB2WC("日本語") << L"]" << endl;
    cout << "JP[" << WC2MB(L"日本語") << "]" << endl;

    cout << Base64EncodeA("C:\\Windows\\system32\\drivers\\hosts") << endl;
    cout << Base64DecodeA("QzpcV2luZG93c1xzeXN0ZW0zMlxkcml2ZXJzXGhvc3Rz") << endl;

    cout << URLEncodeA("https://company.com/s /p+/m-/u_/d$") << endl;
    cout << URLDecodeA("https%3A%2F%2Fcompany.com%2Fs%20%2Fp%2B%2Fm-%2Fu_%2Fd%24") << endl;

    wcout << TrimW(L" \t [a][\t][ ][b]\t \t") << std::endl;

    auto hosts = STCTimeToUTCMilliSecW(L"C:\\Windows\\System32\\Drivers\\etc\\hosts");
    cout << hosts << endl;

    // 1670547095000
    // 1670547095000
    stringstream ss;
    ss << hosts;
    auto s{ ss.str() };

    cout << s << endl;

    FILETIME ftCreate, ftAccess, ftWrite;
    PathToWinFileTimes(L"C:\\Windows\\System32\\Drivers\\etc\\hosts", &ftCreate, &ftAccess, &ftWrite);

    // 133150206954262405
    // 133150206954262405
    auto crtW100ns = WinFileTimeToWinFileTime100ns(ftCreate);
    cout << crtW100ns << endl;

    // 1670547095426
    // 1670547095426
    auto crtUtcMSec = WinFileTimeToUtcMillis(ftCreate);
    cout << crtUtcMSec << endl;

    auto utcMSec = WinFileTime100nsToUtcMillis(crtW100ns);
    cout << utcMSec << endl;

    auto utcW100ns = UtcMillisToWinFileTime100ns(utcMSec);
    cout << utcW100ns << endl;

    FILETIME ftTemp;
    UtcMillisToWinFileTime(utcMSec, &ftTemp);
    auto tempW100ns = WinFileTimeToWinFileTime100ns(ftTemp);
    cout << tempW100ns << endl;

    wcout << "DONE" << endl;
}

mutex lock_map;
unordered_map<string, shared_ptr<mutex>> mutex_map;

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
    //std::vector<BYTE> aesIV{ secureKeyStr.begin(), secureKeyStr.begin() + 16 };

    // test-encrypt-str.ps1 で作成した DATA の BASE64 文字列
    std::string concatB64Str{ "60sNtN3sCNXh2uKRWFAK5M2KiQYxNNO0N/JZRHSL20Y=" };

    // DATA の BASE64 文字列をデコード
    std::string concatStr = Base64DecodeA(concatB64Str);
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

void test4()
{
    //std::wregex pattern{ LR"(.*\\(desktop\.ini|autorun\.inf|thumbs\.db|\.DS_Store)$)", std::regex_constants::icase };
    //std::wregex pattern{ L"(.*\\(desktop\.ini|autorun\.inf|thumbs\.db|\.DS_Store)$)", std::regex_constants::icase };
    std::wregex pattern{ L".*\\\\(desktop\\.ini|autorun\\.inf|thumbs\\.db|\\.DS_Store)$", std::regex_constants::icase };

    if (std::regex_search(L"abc\\desktop.ini", pattern))
    {
        puts("hit");
    }
    else
    {
        puts("no");
    }

    int iii = 0;
    iii++;
}

std::mutex test5_guard;

void test5_worker()
{
    std::lock_guard<std::mutex> lock_(test5_guard);

}

void test5()
{
    const chrono::steady_clock::time_point start{ chrono::steady_clock::now() };

    thread t1(test5_worker);
    thread t2(test5_worker);
    thread t3(test5_worker);

    t1.join();
    t2.join();
    t3.join();

    const chrono::steady_clock::time_point end{ chrono::steady_clock::now() };
    const auto duration{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };

    std::cout << duration.count() << std::endl;
}

void test6()
{
    struct A
    {
        int i;
        A(int x) : i(x)
        {
            std::cout << "CONSTRACT: " << i << std::endl;
        }
        A(const A& other) : i(other.i)
        {
            std::cout << "COPY CONSTRACT: " << i << std::endl;
        }
        ~A()
        {
            std::cout << "DESTRACT: " << i << std::endl;
        }
    };

    std::vector<A> arr;

    arr.emplace_back( 1 );
    arr.emplace_back( 2 );
    arr.emplace_back( 3 );

    std::cout << "done" << std::endl;
}

void test7()
{
    std::vector<char> cs{ '1', '2', '3', '4', '5' };
    std::vector<char> cs2{ cs.begin() + 5, cs.end() };

    int iii = 0;
}

int main()
{
    // chcp 65001
    ::SetConsoleOutputCP(CP_UTF8);
    //std::locale::global(std::locale(""));
    _wsetlocale(LC_ALL, L"");
    wcout.imbue(locale(""));
    wcerr.imbue(locale(""));
    //cout.imbue(locale(""));
    cerr.imbue(locale(""));

    //test1();
    //test2();
    //test3();
    //test4();
    //test5();
    //test6();
    test7();

    return EXIT_SUCCESS;
}

// EOF