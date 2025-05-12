#include "WinCseLib.h"
#include "Protect.hpp"
#include <iostream>

using namespace CSELIB;

struct Shared_Simple : public SharedBase { };

struct Shared_Multipart : public SharedBase
{
    Shared_Multipart(int i)
    {
        std::wcout << L"construct i=" << i << std::endl;
    }
};

static struct ShareStore<Shared_Simple> gSimpleDB;
static struct ShareStore<Shared_Multipart> gMultipartDB;

static void worker1(int id, const std::wstring& key, int sec)
{
    UnprotectedShare<Shared_Simple> unlockLocal{ &gSimpleDB, key };
    {
        const auto lockedLocal{ unlockLocal.lock() };

        std::wcerr << __FUNCTIONW__ << L' ' << key << L" id=" << id << L" sleep(" << sec << L") in ..." << std::endl;
        ::Sleep(1000 * sec);
        std::wcerr << __FUNCTIONW__ << L' ' << key << L" id=" << id << L" sleep(" << sec << L") out" << std::endl;
    }
}

static void worker2(int id, const std::wstring& key, int sec)
{
    UnprotectedShare<Shared_Simple> unlockLocal{ &gSimpleDB, key };
    {
        const auto lockedLocal{ unlockLocal.lock() };

        std::wcerr << __FUNCTIONW__ << L' ' << key << L" id=" << id << L" sleep(" << sec << L") in ..." << std::endl;
        ::Sleep(1000 * sec);
        std::wcerr << __FUNCTIONW__ << L' ' << key << L" id=" << id << L" sleep(" << sec << L") out" << std::endl;
    }
}

void t_WinCseLib_Protect()
{
    std::thread t1(worker1, 1, L"file1.txt", 10);
    std::thread t2(worker2, 2, L"file1.txt", 10);
    std::thread t3(worker1, 3, L"file2.txt", 0);
    std::thread t4(worker2, 4, L"file2.txt", 0);
    std::thread t5(worker1, 5, L"file3.txt", 2);
    std::thread t6(worker2, 6, L"file3.txt", 2);

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();

    std::wcout << L"done." << std::endl;
}

// EOF