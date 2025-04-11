// test-WinCseLib.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
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
#include <filesystem>

using namespace WCSE;
using namespace std;

void test1()
{
    wcout << MB2WC("abc 012") << endl;
    cout << WC2MB(L"abc 012") << endl;

    wcout << L"JP[" << MB2WC("日本語") << L"]" << endl;
    cout << "JP[" << WC2MB(L"日本語") << "]" << endl;

    std::string tmp;
    if (Base64EncodeA("C:\\Windows\\system32\\drivers\\hosts", &tmp))
    {
        cout << tmp << endl;
    }

    if (Base64DecodeA("QzpcV2luZG93c1xzeXN0ZW0zMlxkcml2ZXJzXGhvc3Rz", &tmp))
    {
        cout << tmp << endl;
    }

    //cout << URLEncodeA("https://company.com/s /p+/m-/u_/d$") << endl;
    //cout << URLDecodeA("https%3A%2F%2Fcompany.com%2Fs%20%2Fp%2B%2Fm-%2Fu_%2Fd%24") << endl;

    wcout << TrimW(L" \t [a][\t][ ][b]\t \t") << std::endl;

    auto hosts = STCTimeToUTCMilliSecW(L"C:\\Windows\\System32\\Drivers\\etc\\hosts");
    cout << hosts << endl;

    /*
    1670547095000
    1670547095000
    133150206954262405
    1670547095426
    1670547095426
    133150206954260000
    133150206954260000
    */
    ostringstream ss;
    ss << hosts;
    auto s{ ss.str() };

    cout << s << endl;

    FILETIME ftCreate, ftAccess, ftWrite;
    PathToWinFileTimes(L"C:\\Windows\\System32\\Drivers\\etc\\hosts", &ftCreate, &ftAccess, &ftWrite);

    auto crtW100ns = WinFileTimeToWinFileTime100ns(ftCreate);
    cout << crtW100ns << endl;

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

void test4()
{
    PCWSTR pats = L"\\\\(desktop\\.ini|autorun\\.inf|(eh)?thumbs\\.db|AlbumArtSmall\\.jpg|folder\\.(ico|jpg|gif)|\\.DS_Store)$";

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

        std::wcout << L"isObject: ";
        std::wcout << BOOL_CSTRW(objKey.isObject());
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

        std::wcout << L"isObject: ";
        std::wcout << BOOL_CSTRW(objKey.isObject());
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

    auto orig = ObjectKey{ L"bucket", L"key" };
    ObjectKey dest = std::move(orig);

    std::wcout << orig.c_str() << std::endl;
    std::wcout << dest.c_str() << std::endl;

    std::cout << "done." << std::endl;
}

void test12_worker(bool del)
{
    HANDLE h = ::CreateFileW
    (
        L"test12.tmp",
        GENERIC_WRITE | DELETE, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    Sleep(1000);

    SetLastError(0);

    if (del)
    {
        Sleep(1000);

        FILE_DISPOSITION_INFO DispositionInfo = { 0 };

        DispositionInfo.DeleteFile = TRUE;

        const auto rc = SetFileInformationByHandle(h,
            FileDispositionInfo, &DispositionInfo, sizeof(DispositionInfo));

        const auto lerr = ::GetLastError();

        std::cout << "delete=" << rc << " lerr=" << lerr << endl;
    }
    else
    {
        char buf[] = "abcde";
        DWORD wn;
        const auto rc = WriteFile(h, buf, sizeof(buf), &wn, NULL);
        const auto lerr = ::GetLastError();

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

    virtual void foo()
    {
        std::cout << "Test15Base::foo" << std::endl;
    }
};

class Test15 : public Test15Base
{
public:
    int myInt() const override
    {
        return 15;
    }

    void foo() override
    {
        Test15Base::foo();

        std::cout << "Test15::foo" << std::endl;
    }
};

void test15()
{
    Test15 o;
    o.printInt();
    o.foo();

    std::cout << "done." << std::endl;
}

void test16_b(FileHandle v)
{
    std::cout << "v1=" << BOOL_CSTRA(v.valid()) << std::endl;
}

void test16()
{
    HANDLE h = ::CreateFile(LR"(C:\Windows\System32\drivers\etc\hosts)", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

    FileHandle a;
    std::cout << "a1=" << BOOL_CSTRA(a.valid()) << std::endl;
    a = h;
    std::cout << "a2=" << BOOL_CSTRA(a.valid()) << std::endl;

    test16_b(std::move(a));
    std::cout << "a3=" << BOOL_CSTRA(a.valid()) << std::endl;

    FileHandle b = ::CreateFile(LR"(C:\Windows\System32\drivers\etc\hosts)", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    std::cout << "b1=" << BOOL_CSTRA(b.valid()) << std::endl;

    FileHandle c;
    std::cout << "c1=" << BOOL_CSTRA(c.valid()) << std::endl;

    c = std::move(b);
    std::cout << "b2=" << BOOL_CSTRA(b.valid()) << std::endl;
    std::cout << "c2=" << BOOL_CSTRA(c.valid()) << std::endl;

    std::cout << "done." << std::endl;
}

void test16_2()
{
    FileHandle a = ::CreateFile(LR"(C:\Windows\System32\drivers\etc\hosts)", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

    a = ::CreateFile(LR"(C:\Windows\System32\drivers\etc\services)", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

    HANDLE n = INVALID_HANDLE_VALUE;
    a = n;

    std::cout << "done." << std::endl;
}

void test17()
{
    FILETIME ft1, ft2, ft3;

    bool b1 = PathToWinFileTimes(LR"(C:\Windows)", &ft1, &ft2, &ft3); 
    bool b2 = PathToWinFileTimes(LR"(C:\Windows\System32\drivers\etc\hosts)", &ft1, &ft2, &ft3);

    bool b3 = PathToWinFileTimes(LR"(C:\Windows)", &ft1, &ft2, &ft3); 
    bool b4 = PathToWinFileTimes(LR"(C:\Windows\System32\drivers\etc\hosts)", &ft1, &ft2, &ft3);

    FSP_FSCTL_FILE_INFO fi;

    bool b5 = PathToFileInfo(LR"(C:\NotFound)", &fi);
    bool b6 = PathToFileInfo(LR"(C:\NotFound\System32\drivers\etc\hosts)", &fi);

    bool b7 = PathToFileInfo(LR"(C:\NotFound)", &fi);
    bool b8 = PathToFileInfo(LR"(C:\NotFound\System32\drivers\etc\hosts)", &fi);

    std::cout << BOOL_CSTRA(b1) << std::endl;
    std::cout << BOOL_CSTRA(b2) << std::endl;
    std::cout << BOOL_CSTRA(b3) << std::endl;
    std::cout << BOOL_CSTRA(b4) << std::endl;
    std::cout << BOOL_CSTRA(b5) << std::endl;
    std::cout << BOOL_CSTRA(b6) << std::endl;
    std::cout << BOOL_CSTRA(b7) << std::endl;
    std::cout << BOOL_CSTRA(b8) << std::endl;

    std::cout << "done." << std::endl;
}

void test18()
{
    DWORD a = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DIRECTORY;
    std::cout << a << std::endl;

    a &= ~FILE_ATTRIBUTE_NORMAL;
    std::cout << a << std::endl;

    a &= ~FILE_ATTRIBUTE_NORMAL;
    std::cout << a << std::endl;

    std::cout << "done." << std::endl;
}

void test19()
{
    HANDLE hFile = ::CreateFileW(
        LR"(C:\Windows)",
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        std::cout << ::GetLastError() << std::endl;
    }
    else
    {
        std::cout << "win file" << std::endl;

        ::CloseHandle(hFile);
    }

    HANDLE hDir = ::CreateFileW(
        LR"(C:\Windows)",
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE)
    {
        std::cout << ::GetLastError() << std::endl;
    }
    else
    {
        std::cout << "win dir" << std::endl;

        ::CloseHandle(hDir);
    }

    std::cout << "done." << std::endl;
}

class If21 {
public:
    virtual void printInt(int, int) = 0;
    virtual ~If21() {}
};

class Klass21 : public If21 {
public:
    void printInt(int i, int j) override {
        std::cout << "Klass21::printInt called with i = " << i << ", j = " << j << std::endl;
    }
};

// テンプレートを使用した安全なメンバー関数呼び出し
template<typename ClassType, typename MethodType, typename... Args>
void test21_sub(ClassType* obj, MethodType method, Args... args)
{
    if (obj && method)
    {
        (obj->*method)(args...);  // メンバー関数を安全に呼び出し
    }
    else
    {
        std::cout << "Invalid method or object!" << std::endl;
    }
}

void test21()
{
    Klass21 k21;

    // Klass21::printInt を呼び出すためのメンバー関数ポインタ
    void (If21::*PrintInt)(int, int) = &If21::printInt;

    // test21_sub を使ってメンバー関数を呼び出し
    test21_sub(&k21, PrintInt, 1, 2);

    std::cout << "done." << std::endl;
}

void test23()
{
    const auto a = ObjectKey::fromPath(L"");
    const auto b = ObjectKey::fromPath(L"a");
    const auto c = ObjectKey::fromPath(L"a/");
    const auto d = ObjectKey::fromPath(L"a/b");
    const auto e = ObjectKey::fromPath(L"a/b/");
    const auto f = ObjectKey::fromPath(L"a/b/c");
    const auto g = ObjectKey::fromPath(L"a/b/c/");

    std::wcout << a.str() << std::endl;
    std::wcout << b.str() << std::endl;
    std::wcout << c.str() << std::endl;
    std::wcout << d.str() << std::endl;
    std::wcout << e.str() << std::endl;
    std::wcout << f.str() << std::endl;
    std::wcout << g.str() << std::endl;

    std::wstring parentDir;
    std::wstring filename;

    const auto r = SplitPath(g.str(), &parentDir, &filename);
    std::wcout << r << std::endl;

    std::cout << "done." << std::endl;
}

void test24()
{
    wchar_t path[MAX_PATH];
    GetCurrentDirectoryW(_countof(path), path);
    wcout << path << endl;

    HANDLE h = ::CreateFileW(L"aa.txt", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    _ASSERT(h != INVALID_HANDLE_VALUE);

    BOOL b = ::DeleteFileW(L"aa.txt");
    std::cout << (b ? "true" : "false") << std::endl;
    std::cout << ::GetLastError() << std::endl;

    b = ::WriteFile(h, "aaa", 3, NULL, NULL);
    std::cout << (b ? "true" : "false") << std::endl;
    std::cout << ::GetLastError() << std::endl;

    ::CloseHandle(h);
}

void test25()
{
    wchar_t path[MAX_PATH];
    GetCurrentDirectoryW(_countof(path), path);
    wcout << path << endl;

    HANDLE h = ::CreateFileW(L"aa.txt", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    _ASSERT(h != INVALID_HANDLE_VALUE);

    BOOL b = DeleteFilePassively(L"aa.txt");
    std::cout << (b ? "true" : "false") << std::endl;
    std::cout << ::GetLastError() << std::endl;

    /*
    std::error_code ec;
    std::filesystem::remove("aa.txt", ec);
    std::cout << ec << std::endl;
    std::cout << ::GetLastError() << std::endl;
    */

    b = ::WriteFile(h, "aaa", 3, NULL, NULL);
    std::cout << (b ? "true" : "false") << std::endl;
    std::cout << ::GetLastError() << std::endl;

    ::CloseHandle(h);

    b = DeleteFilePassively(L"aa.txt");
    std::cout << (b ? "true" : "false") << std::endl;
    std::cout << ::GetLastError() << std::endl;
}

void test26()
{
    forEachFiles(L"..\\..", [](const auto&, const auto& fullPath)
    {
        std::wcout << fullPath << std::endl;
    });

    forEachDirs(L"..\\..", [](const auto&, const auto& fullPath)
    {
        std::wcout << fullPath << std::endl;
    });
}

void test27()
{
    std::error_code ec;

    std::filesystem::create_directory("emptydir", ec);
    std::cout << ::GetLastError() << std::endl;
    std::cout << ec << std::endl;
    std::cout << ec.message() << std::endl;
    std::cout << !ec << std::endl;

    std::filesystem::remove("emptydir", ec);
    std::cout << ::GetLastError() << std::endl;
    std::cout << ec << std::endl;
    std::cout << ec.message() << std::endl;
    std::cout << !ec << std::endl;

    std::filesystem::remove("not-emptydir", ec);
    std::cout << ::GetLastError() << std::endl;
    std::cout << ec << std::endl;
    std::cout << ec.message() << std::endl;
    std::cout << !ec << std::endl;

    if (ec)
    {
        std::cout << "err" << std::endl;
    }
}

template <typename Container>
std::wstring t28JoinStrings(const Container& tokens, wchar_t sep, bool ignoreEmpty)
{
    std::wostringstream ss;
    bool first = true;

    for (const auto& token : tokens)
    {
        if (ignoreEmpty && token.empty()) {
            continue;
        }

        if (!first) {
            ss << sep;
        }
        first = false;

        ss << token;
    }

    return ss.str();
}

void test28()
{
    std::vector<std::wstring> vec = {L"りんご", L"", L"バナナ"};

    std::wcout << t28JoinStrings(vec, L',', false) << std::endl;
    std::wcout << t28JoinStrings(vec, L',', true) << std::endl;
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

    test1();
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
    //test15();
    //test16();
    //test16_2();
    //test17();
    //test18();
    //test19();

    int test20();
    //test20();

    //test21();

    int test22();
    //test22();

    //test23();
    //test24();
    //test25();

    //test26();
    //test27();

    test28();

    return EXIT_SUCCESS;
}

// EOF