#include <iostream>
#include <Windows.h>
#include "AwsS3.hpp"

using namespace WCSE;

void test4()
{
    std::wstring parentDir;
    std::wstring filename;

    const wchar_t* keys[] =
    {
        L"",
        L"dir",
        L"dir/",
        L"dir/key.txt",
        L"dir/key.txt/",
        L"dir/subdir/key.txt",
        L"dir/subdir/key.txt/"
    };

    for (const auto key: keys)
    {
        bool b = WCSE::SplitPath(key, &parentDir, &filename);

        wprintf(L"key=[%s] b=[%s] dir=[%s] file=[%s]\n",
            key, BOOL_CSTRW(b), parentDir.c_str(), filename.c_str());
    }
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

int wmain(int argc, wchar_t** argv)
{
    int test1(int argc, wchar_t** argv);
	test1(argc, argv);

	//test2();
    //test3();
    //test4();

    int test5(int argc, wchar_t** argv);
    //test5(argc, argv);

    test13();

    return 0;
}

// EOF