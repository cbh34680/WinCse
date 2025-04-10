// test-WinCse-aws-s3.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#pragma comment(lib, "WinCseLib.lib")
#pragma comment(lib, "WinCse-aws-s3.lib")

#include "WinCseLib.h"

#include <iostream>

using namespace WCSE;

struct ImmediateWorker : public IWorker
{
    bool addTask(CALLER_ARG ITask* argTask)
    {
        argTask->run(START_CALLER0);
        delete argTask;
        return false;
    }
};

static bool app_tempdir(std::wstring* tmpDir);

extern "C"
{
    __declspec(dllimport) ICSDevice* NewCSDevice(
        PCWSTR argTempDir, PCWSTR argIniSection, NamedWorker workers[]);
}

static void test1_1(ICSDevice* cs)
{
    DirInfoListType buckets;
    if (cs->listBuckets(START_CALLER &buckets))
    {
        for (const auto& bucket: buckets)
        {
            const auto bucketName{ bucket->FileNameBuf };

            DirInfoListType objs;

            if (!cs->listObjects(START_CALLER ObjectKey{ bucketName, L"" }, &objs))
            {
                continue;
            }

            for (const auto& obj: objs)
            {
                if (FA_IS_DIRECTORY(obj->FileInfo.FileAttributes))
                {
                    continue;
                }

                if (obj->FileInfo.FileSize >= FILESIZE_1MiBu)
                {
                    // !!!
                    continue;
                }

                FSP_FSCTL_FILE_INFO fileInfo{};

                if (!cs->headObject(START_CALLER ObjectKey{ bucketName, obj->FileNameBuf }, &fileInfo))
                {
                    continue;
                }

                UINT32 CreateOptions = FILE_ATTRIBUTE_NORMAL;
                UINT32 GrantedAccess = GENERIC_READ;

                ObjectKey objKey{ bucketName, obj->FileNameBuf };
                std::wcout << L"objKey=" << objKey.c_str() << std::endl;

                CSDeviceContext* ctx = cs->open(START_CALLER objKey, CreateOptions, GrantedAccess, fileInfo);
                if (!ctx)
                {
                    continue;
                }

                NTSTATUS ntstatus = STATUS_UNSUCCESSFUL;

                char buffer[512] = {};
                UINT64 offset = 0ULL;
                ULONG numread = 0UL;
                ULONG total = 0UL;

                do
                {
                    offset += numread;
                    total += numread;

                    numread = 0;

                    // readObject_Simple() でしか動かない
                    ntstatus = cs->readObject(START_CALLER ctx, buffer, offset, sizeof(buffer), &numread);

                    std::cout.write(buffer, numread);
                }
                while (NT_SUCCESS(ntstatus));

                std::cout << std::endl;

                std::cout << "total: " << total << std::endl;

                cs->close(START_CALLER ctx);

                break;
            }
        }
    }
}

int test1(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        std::wcerr << L"Usage: " << argv[0] << L" TraceLogDir WorkDir" << std::endl;
        return 1;
    }

    _wsetlocale(LC_ALL, L"");

    // sstream に送信した数値がカンマ区切りになってしまう
    //std::locale::global(std::locale(""));

    std::wcout.imbue(std::locale(""));
    std::wcerr.imbue(std::locale(""));
    setlocale(LC_ALL, "");
    ::SetConsoleOutputCP(CP_UTF8);

#ifdef _DEBUG
    ::_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    //OpenEvent(EVENT_ALL_ACCESS, FALSE, L"Global\\WinCse\\test");

    std::wstring tmpDir;
    app_tempdir(&tmpDir);

    if (CreateLogger(tmpDir.c_str(), argv[1], L"aws-s3"))
    {
        ImmediateWorker worker;

        NamedWorker workers[] = {
            { L"delayed", &worker },
            { L"idle", &worker },
            { L"timer", &worker },
            { nullptr, nullptr },
        };

        ICSDevice* cs = (ICSDevice*)NewCSDevice(tmpDir.c_str(), L"default", workers);
        APP_ASSERT(cs);

        FSP_FSCTL_VOLUME_PARAMS vp{ };

        cs->PreCreateFilesystem(nullptr, argv[2], &vp);

        test1_1(cs);

        delete cs;
    }

    DeleteLogger();

#ifdef _DEBUG
    ::_CrtDumpMemoryLeaks();
#endif

    return 0;
}

static bool app_tempdir(std::wstring* pTmpDir)
{
    wchar_t tmpdir[MAX_PATH];
    const auto err = ::GetTempPath(MAX_PATH, tmpdir);
    APP_ASSERT(err != 0);

    if (tmpdir[wcslen(tmpdir) - 1] == L'\\')
    {
        tmpdir[wcslen(tmpdir) - 1] = L'\0';
    }

    wcscat_s(tmpdir, L"\\WinCse");

    if (!MkdirIfNotExists(tmpdir))
    {
        std::wcerr << tmpdir << L": dir not exists" << std::endl;
        return false;
    }

    *pTmpDir = tmpdir;

    return true;
}


/*
* 
* [32]
* 
Detected memory leaks!
Dumping objects ->
{193} normal block at 0x00000254D9C84F70, 128 bytes long.
Data: <ｰ ﾈﾙT   ｰ ﾈﾙT   > B0 0F C8 D9 54 02 00 00 B0 0F C8 D9 54 02 00 00 
{192} normal block at 0x00000254D9C82FE0, 16 bytes long.
Data: <ﾀ)ｿ             > C0 29 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{191} normal block at 0x00000254D9C80FB0, 72 bytes long.
Data: <ｰ ﾈﾙT   ｰ ﾈﾙT   > B0 0F C8 D9 54 02 00 00 B0 0F C8 D9 54 02 00 00 
{190} normal block at 0x00000254D9C7EFE0, 16 bytes long.
Data: <ｨ)ｿ             > A8 29 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{189} normal block at 0x00000254D9C7CF40, 176 bytes long.
Data: <@ﾏﾇﾙT   @ﾏﾇﾙT   > 40 CF C7 D9 54 02 00 00 40 CF C7 D9 54 02 00 00 
{188} normal block at 0x00000254D9C7AFE0, 16 bytes long.
Data: <X)ｿ             > 58 29 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{187} normal block at 0x00000254D9C78F20, 208 bytes long.
Data: <  ﾇﾙT     ﾇﾙT   > 20 8F C7 D9 54 02 00 00 20 8F C7 D9 54 02 00 00 
{186} normal block at 0x00000254D9C76FE0, 16 bytes long.
Data: <@)ｿ             > 40 29 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{185} normal block at 0x00000254D9C74F70, 128 bytes long.
Data: <  ﾇﾙT     ﾇﾙT   > 90 0F C7 D9 54 02 00 00 90 0F C7 D9 54 02 00 00 
{184} normal block at 0x00000254D9C72FE0, 16 bytes long.
Data: <ﾘ(ｿ             > D8 28 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{183} normal block at 0x00000254D9C70F90, 96 bytes long.
Data: <  ﾇﾙT     ﾇﾙT   > 90 0F C7 D9 54 02 00 00 90 0F C7 D9 54 02 00 00 
{182} normal block at 0x00000254D9C6EFE0, 16 bytes long.
Data: <ﾀ(ｿ             > C0 28 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{181} normal block at 0x00000254D9C6CFE0, 16 bytes long.
Data: <x(ｿ             > 78 28 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{180} normal block at 0x00000254D9C6AFE0, 16 bytes long.
Data: <P(ｿ             > 50 28 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{179} normal block at 0x00000254D9C68FE0, 16 bytes long.
Data: <0(ｿ             > 30 28 BF 87 FF 7F 00 00 00 00 00 00 00 00 00 00 
{177} normal block at 0x00000254D9C64F70, 128 bytes long.
Data: <  ﾆﾙT     ﾆﾙT   > 90 0F C6 D9 54 02 00 00 90 0F C6 D9 54 02 00 00 
{176} normal block at 0x00000254D9C62FE0, 16 bytes long.
Data: <  }             > F0 8A 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
{175} normal block at 0x00000254D9C60F90, 104 bytes long.
Data: <  ﾆﾙT     ﾆﾙT   > 90 0F C6 D9 54 02 00 00 90 0F C6 D9 54 02 00 00 
{174} normal block at 0x00000254D9C5EFE0, 16 bytes long.
Data: <ﾘ }             > D8 8A 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
{173} normal block at 0x00000254D9C5CF70, 128 bytes long.
Data: <  ﾅﾙT     ﾅﾙT   > 90 8F C5 D9 54 02 00 00 90 8F C5 D9 54 02 00 00 
{172} normal block at 0x00000254D9C5AFE0, 16 bytes long.
Data: <  }             > A0 8A 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
{171} normal block at 0x00000254D9C58F90, 104 bytes long.
Data: <  ﾅﾙT     ﾅﾙT   > 90 8F C5 D9 54 02 00 00 90 8F C5 D9 54 02 00 00 
{170} normal block at 0x00000254D9C56FE0, 16 bytes long.
Data: <  }             > 88 8A 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
{169} normal block at 0x00000254D9C54F70, 128 bytes long.
Data: <  ﾅﾙT     ﾅﾙT   > 90 0F C5 D9 54 02 00 00 90 0F C5 D9 54 02 00 00 
{168} normal block at 0x00000254D9C52FE0, 16 bytes long.
Data: <@ }             > 40 8A 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
{167} normal block at 0x00000254D9C50F90, 104 bytes long.
Data: <  ﾅﾙT     ﾅﾙT   > 90 0F C5 D9 54 02 00 00 90 0F C5 D9 54 02 00 00 
{166} normal block at 0x00000254D9C4EFE0, 16 bytes long.
Data: <( }             > 28 8A 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
{165} normal block at 0x00000254D9C4CF70, 128 bytes long.
Data: <  ﾄﾙT     ﾄﾙT   > 90 8F C4 D9 54 02 00 00 90 8F C4 D9 54 02 00 00 
{164} normal block at 0x00000254D9C4AFE0, 16 bytes long.
Data: <  }             > F0 89 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
{163} normal block at 0x00000254D9C48F90, 104 bytes long.
Data: <  ﾄﾙT     ﾄﾙT   > 90 8F C4 D9 54 02 00 00 90 8F C4 D9 54 02 00 00 
{162} normal block at 0x00000254D9C46FE0, 16 bytes long.
Data: <ﾘ }             > D8 89 7D 80 FF 7F 00 00 00 00 00 00 00 00 00 00 
Object dump complete.
*
*/

// EOF