#include "WinCseLib.h"
#include <iostream>

void t_WinCseLib_ObjectKey()
{
    const auto optBucket{ CSELIB::ObjectKey::fromWinPath(L"\\bucket") };
    if (optBucket)
    {
        std::wcout << optBucket->append(L"file.txt").str() << std::endl;
        std::wcout << optBucket->append(L"dir/").str() << std::endl;
    }


    std::wcout << L"done." << std::endl;
}

static void checkObjectKey(const CSELIB::ObjectKey& objKey)
{
    std::wcout << L"isBucket: ";
    std::wcout << BOOL_CSTRW(objKey.isBucket());
    std::wcout << std::endl;

    std::wcout << L"isObject: ";
    std::wcout << BOOL_CSTRW(objKey.isObject());
    std::wcout << std::endl;

    std::wcout << L"meansDir: ";
    std::wcout << BOOL_CSTRW(objKey.meansDir());
    std::wcout << std::endl;

    std::wcout << L"meansFile: ";
    std::wcout << BOOL_CSTRW(objKey.meansFile());
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

    std::wcout << "append(.): ";
    std::wcout << objKey.append(L".").str();
    std::wcout << std::endl;

    std::wcout << "append(..): ";
    std::wcout << objKey.append(L"..").str();
    std::wcout << std::endl;

    std::wcout << L"toDir: ";
    std::wcout << objKey.toDir().str();
    std::wcout << std::endl;

    std::wcout << L"toFile: ";
    std::wcout << objKey.toFile().str();
    std::wcout << std::endl;

    std::wcout << L"toWinPath: ";
    std::wcout << objKey.toWinPath();
    std::wcout << std::endl;

    const auto parentDir{ objKey.toParentDir() };

    std::wcout << L"toParentDir: ";
    if (parentDir)
    {
        std::wcout << parentDir->toDir().str() << std::endl;
    }
    else
    {
        std::wcout << L"*** error ***" << std::endl;
    }

    std::wcout << L"equal?: ";
    const auto winPath{ objKey.toWinPath() };
    const auto optCheckWinKey{ CSELIB::ObjectKey::fromWinPath(winPath) };
    if (optCheckWinKey)
    {
        const auto& checkObjKey{ *optCheckWinKey };

        if (objKey == checkObjKey)
        {
            std::wcout << L"true" << std::endl;
        }
        else
        {
            std::wcout << L"false" << std::endl;
        }
    }
    else
    {
        std::wcout << L"*** error ***" << std::endl;
    }


    std::wcout << std::endl;
}

void t_WinCseLib_ObjectKey_fromPath()
{
    std::vector<std::wstring> arr
    {
        L"",
        L"/",
        L"my-bucket",
        L"my-bucket/",
        L"my-bucket/my-key",
        L"my-bucket/my-key/",
        L"my-bucket/my-key/my-subkey",
        L"my-bucket/my-key/my-subkey/",
    };

    for (const auto& str: arr)
    {
        std::wcout << L"**********" << std::endl;

        std::wcout << L"# [" << str << L"]" << std::endl;
        std::wcout << std::endl;

        auto optObjKey{ CSELIB::ObjectKey::fromPath(str) };
        if (!optObjKey)
        {
            std::wcout << L"INVALID" << std::endl;
            continue;
        }

        auto objKey{ *optObjKey };

        checkObjectKey(objKey);
    }

    std::cout << "done." << std::endl;
}

void t_WinCseLib_ObjectKey_fromWinPath()
{
    std::initializer_list<std::wstring> arr
    {
        L"",
        L"\\",
        L"\\\\\\",
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

        auto optObjKey{ CSELIB::ObjectKey::fromWinPath(str) };
        if (!optObjKey)
        {
            std::wcout << L"INVALID" << std::endl;
            continue;
        }

        auto objKey{ *optObjKey };

        checkObjectKey(objKey);
    }

    std::cout << "done." << std::endl;
}

// EOF