﻿// test-WinCseLib.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#pragma comment(lib, "WinCseLib.lib")

#include "WinCseLib.h"
#include <iostream>
#include <thread>
//#include <map>
#include <regex>
#include <sstream>
#include <deque>
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
    const wchar_t* pats = L"\\\\(desktop\\.ini|autorun\\.inf|(eh)?thumbs\\.db|AlbumArtSmall\\.jpg|folder\\.(ico|jpg|gif)|\\.DS_Store)$";

    //std::wregex pattern{  L".*\\\\(desktop\\.ini|autorun\\.inf|thumbs\\.db|\\.DS_Store)$", std::regex_constants::icase };
    std::wregex pattern{ pats, std::regex_constants::icase };

    std::vector<std::wstring> strs =
    {
        L"abc\\desktop.ini",
        L"abc\\folder.jpg",
        L"abc\\folder.gif",
        L"abc\\thumbs.db",
        L"abc\\ehthumbs.db",
        L"\\albumartsmall.jpg",
        L"\\folder.ico",
    };

    for (const auto& str: strs)
    {
        std::wcout << str << ": ";

        if (std::regex_search(str, pattern))
        {
            std::wcout << L"match" << std::endl;
        }
        else
        {
            std::wcout << L"no match" << std::endl;
        }
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

    std::cout << cs2.data() << std::endl;
    std::cout << "done." << std::endl;
}

void test8()
{
    std::vector<std::vector<std::wstring>> arrs
    {
        { L"",          L"" },
        { L"",          L"my-key" },
        { L"",          L"my-key/" },
        { L"",          L"my-key/my-subkey" },
        { L"",          L"my-key/my-subkey/" },
        { L"my-bucket", L"" },
        { L"my-bucket", L"my-key" },
        { L"my-bucket", L"my-key/" },
        { L"my-bucket", L"my-key/my-subkey" },
        { L"my-bucket", L"my-key/my-subkey/" },
    };

    for (const auto& arr: arrs)
    {
        std::wcout << L"**********" << std::endl;

        std::wcout << L"# [" << arr[0] << L"]" << std::endl;
        std::wcout << L"# [" << arr[1] << L"]" << std::endl;
        std::wcout << std::endl;

        ObjectKey objKey{ arr[0], arr[1] };

        std::wcout << L"valid: ";
        std::wcout << BOOL_CSTRW(objKey.valid());
        std::wcout << std::endl;

        std::wcout << L"hasKey: ";
        std::wcout << BOOL_CSTRW(objKey.hasKey());
        std::wcout << std::endl;

        std::wcout << L"bucket: ";
        std::wcout << objKey.bucket();
        std::wcout << std::endl;

        std::wcout << L"key: ";
        std::wcout << objKey.key();
        std::wcout << std::endl;

        std::wcout << L"str: ";
        std::wcout << objKey.str();
        std::wcout << std::endl;

        std::wcout << L"c_str: ";
        std::wcout << objKey.c_str();
        std::wcout << std::endl;

        std::cout << "bucketA: ";
        std::cout << objKey.bucketA();
        std::cout << std::endl;

        std::cout << "keyA: ";
        std::cout << objKey.keyA();
        std::cout << std::endl;

        std::cout << "strA: ";
        std::cout << objKey.strA();
        std::cout << std::endl;

        std::wcout << L"meansDir: ";
        std::wcout << BOOL_CSTRW(objKey.meansDir());
        std::wcout << std::endl;

        std::wcout << std::endl;
    }

    std::cout << "done." << std::endl;
}

void test9()
{
    std::vector<std::wstring> arr
    {
        L"",
        L"\\",
        L"\\my-bucket",
        L"\\my-bucket\\",
        L"\\my-bucket\\my-key",
        L"\\my-bucket\\my-key\\",
        L"\\my-bucket\\my-key\\my-subkey",
        L"\\my-bucket\\my-key\\my-subkey\\",
    };

    for (const auto& str: arr)
    {
        std::wcout << L"**********" << std::endl;

        std::wcout << L"# [" << str << L"]" << std::endl;
        std::wcout << std::endl;

        ObjectKey objKey{ ObjectKey::fromWinPath(str) };

        std::wcout << L"valid: ";
        std::wcout << BOOL_CSTRW(objKey.valid());
        std::wcout << std::endl;

        std::wcout << L"hasKey: ";
        std::wcout << BOOL_CSTRW(objKey.hasKey());
        std::wcout << std::endl;

        std::wcout << L"bucket: ";
        std::wcout << objKey.bucket();
        std::wcout << std::endl;

        std::wcout << L"key: ";
        std::wcout << objKey.key();
        std::wcout << std::endl;

        std::wcout << L"str: ";
        std::wcout << objKey.str();
        std::wcout << std::endl;

        std::wcout << L"c_str: ";
        std::wcout << objKey.c_str();
        std::wcout << std::endl;

        std::cout << "bucketA: ";
        std::cout << objKey.bucketA();
        std::cout << std::endl;

        std::cout << "keyA: ";
        std::cout << objKey.keyA();
        std::cout << std::endl;

        std::cout << "strA: ";
        std::cout << objKey.strA();
        std::cout << std::endl;

        std::wcout << L"meansDir: ";
        std::wcout << BOOL_CSTRW(objKey.meansDir());
        std::wcout << std::endl;

        std::wcout << L"meansFile: ";
        std::wcout << BOOL_CSTRW(objKey.meansFile());
        std::wcout << std::endl;

        if (objKey.valid())
        {
            std::wcout << L"toDir: ";
            std::wcout << objKey.toDir().str();
            std::wcout << std::endl;

            const auto parentDir{ objKey.toParentDir() };

            std::wcout << L"toParentDir: ";
            if (parentDir)
            {
                std::wcout << parentDir->toDir().str();
            }
            else
            {
                std::wcout << L"*** error ***";
            }
            std::wcout << std::endl;
        }

        std::wcout << std::endl;
    }

    std::cout << "done." << std::endl;
}

#include <iostream>
#include <deque>
#include <algorithm>  // std::sortを使用するために必要
#include <memory>  // std::unique_ptrを使用するために必要

// カスタムコンパレータ関数
bool customComparator(const std::unique_ptr<int>& a, const std::unique_ptr<int>& b) {
    return *a < *b;  // ポインタの中身を比較
}

int test10() {
    std::deque<std::unique_ptr<int>> myDeque;

    // std::unique_ptrのデックを初期化
    myDeque.push_back(std::make_unique<int>(5));
    myDeque.push_back(std::make_unique<int>(2));
    myDeque.push_back(std::make_unique<int>(9));
    myDeque.push_back(std::make_unique<int>(1));
    myDeque.push_back(std::make_unique<int>(5));
    myDeque.push_back(std::make_unique<int>(6));

    // ソート前のデックの内容を表示
    std::cout << "Before sorting: ";
    for (const auto& ptr : myDeque) {
        std::cout << *ptr << " ";
    }
    std::cout << std::endl;

    // std::sortを使用してカスタムコンパレータでソート
    std::sort(myDeque.begin(), myDeque.end(), customComparator);

    // ソート後のデックの内容を表示
    std::cout << "After sorting: ";
    for (const auto& ptr : myDeque) {
        std::cout << *ptr << " ";
    }
    std::cout << std::endl;

    return 0;
}

void test11()
{
    bool b1 = ObjectKey{ L"a", L"1" } < ObjectKey{ L"b", L"1" };
    bool b2 = ObjectKey{ L"b", L"1" } < ObjectKey{ L"a", L"1" };
    bool b3 = ObjectKey{ L"a", L"1" } < ObjectKey{ L"a", L"1" };
    bool b4 = ObjectKey{ L"a", L"1" } < ObjectKey{ L"a", L"2" };
    bool b5 = ObjectKey{ L"a", L"2" } < ObjectKey{ L"a", L"1" };

    std::cout << (b1 ? "true" : "false") << std::endl;
    std::cout << (b2 ? "true" : "false") << std::endl;
    std::cout << (b3 ? "true" : "false") << std::endl;
    std::cout << (b4 ? "true" : "false") << std::endl;
    std::cout << (b5 ? "true" : "false") << std::endl;

    std::cout << "done." << std::endl;
}

void test12_worker(bool del)
{
    HANDLE h = CreateFileW(L"test12.tmp", GENERIC_WRITE | DELETE, FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    Sleep(1000);

    SetLastError(0);

    if (del)
    {
        Sleep(1000);

        FILE_DISPOSITION_INFO DispositionInfo = { 0 };

        DispositionInfo.DeleteFile = TRUE;

        const auto rc = SetFileInformationByHandle(h,
            FileDispositionInfo, &DispositionInfo, sizeof DispositionInfo);

        const auto lerr = GetLastError();

        std::cout << "delete=" << rc << " lerr=" << lerr << endl;
    }
    else
    {
        char buf[] = "abcde";
        DWORD wn;
        const auto rc = WriteFile(h, buf, sizeof(buf), &wn, NULL);
        const auto lerr = GetLastError();

        FlushFileBuffers(h);

        std::cout << "write=" << rc << " wn=" << wn << " lerr=" << lerr << endl;
    }

    Sleep(1000);

    CloseHandle(h);
}

void test12()
{
    wchar_t path[MAX_PATH];
    GetCurrentDirectoryW(_countof(path), path);
    wcout << path << endl;

    thread t1(test12_worker, true);
    thread t2(test12_worker, false);

    t1.join();
    t2.join();

    cout << "done." << endl;
}

struct Shared_Simple : public SharedBase { };

struct Shared_Multipart : public SharedBase
{
    Shared_Multipart(int i)
    {
        std::wcout << L"construct i=" << i << std::endl;
    }
};

struct ShareStore<Shared_Simple> gSimpleDB;
struct ShareStore<Shared_Multipart> gMultipartDB;

void test13_worker(int id, const std::wstring& key, int sec)
{
    UnprotectedShare<Shared_Simple> unlockLocal(&gSimpleDB, key);

    {
        const auto lockedLocal{ unlockLocal.lock() };

        std::wcerr << key << L" id=" << id << L" sleep(" << sec << L") in ..." << std::endl;
        ::Sleep(1000 * sec);
        std::wcerr << key << L" id=" << id << L" sleep(" << sec << L") out" << std::endl;
    }
}

void test13()
{
    std::thread t1(test13_worker, 1, L"file1.txt", 10);
    std::thread t2(test13_worker, 2, L"file1.txt", 10);
    std::thread t3(test13_worker, 3, L"file2.txt", 0);
    std::thread t4(test13_worker, 4, L"file2.txt", 0);
    std::thread t5(test13_worker, 5, L"file3.txt", 2);
    std::thread t6(test13_worker, 6, L"file3.txt", 2);

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();

    std::wcout << L"done." << std::endl;
}

class Klass
{
    std::string mName;

public:
    Klass(std::string name) : mName(name)
    {
        std::cout << "contruct " << mName << std::endl;
    }

    ~Klass()
    {
        std::cout << "destruct " << mName << std::endl;
    }
};

void test14()
{
    Klass a{ "a" };
    Klass b{ "b" };
}

class Test15Base
{
public:
    virtual int myInt() const = 0;
    void printInt()
    {
        std::cout << myInt() << std::endl;
    }
};

class Test15 : public Test15Base
{
public:
    int myInt() const override
    {
        return 15;
    }
};

void test15()
{
    Test15 o;
    o.printInt();
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
    //test7();
    //test8();
    //test9();
    //test10();
    //test11();
    //test12();
    //test13();
    //test14();
    test15();

    return EXIT_SUCCESS;
}

// EOF