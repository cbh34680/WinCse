// main.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#pragma comment(lib, "winfsp-x64.lib")
#pragma comment(lib, "aws-cpp-sdk-core.lib")
#pragma comment(lib, "aws-cpp-sdk-s3.lib")
#pragma comment(lib, "Crypt32.lib")             // CryptBinaryToStringA
#pragma comment(lib, "Dbghelp.lib")             // SymInitialize

#include "WinCseLib.h"
#include "Logger.hpp"
#include "DelayedWorker.hpp"
#include "IdleWorker.hpp"
#include "AwsS3.hpp"
#include "WinCse.hpp"
#include <csignal>
#include <dbghelp.h>


static bool app_tempdir(std::wstring& wtmpdir);
static void app_terminate();
static void app_abort(int signum);

static ILogger* gLogger;

static int app_main(int argc, wchar_t** argv, WCHAR* progname, const wchar_t* iniSection, const wchar_t* traceLogDir)
{
    // これやらないと日本語が出力できない
    _wsetlocale(LC_ALL, L"");

    std::signal(SIGABRT, app_abort);

    // スレッドでの捕捉されない例外を拾えるかも
    std::set_terminate(app_terminate);

    std::wstring wtmpdir;
    if (!app_tempdir(wtmpdir))
    {
        std::cerr << "fault: app_tempdir" << std::endl;
        return EXIT_FAILURE;
    }

    std::wcout << L"use Tempdir: " << wtmpdir << std::endl;

    const auto tmpdir{ wtmpdir.c_str() };

    Logger logger(tmpdir);

    if (traceLogDir)
    {
        if (!logger.SetOutputDir(traceLogDir))
        {
            std::wcerr << L"warn: SetTraceLogDir" << std::endl;
            std::wcerr << traceLogDir << std::endl;

            // ログが指定されたディレクトリに出力出来ない場合でも続行
            //return EXIT_FAILURE;
        }
    }

    gLogger = &logger;

    wchar_t defaultIniSection[] = L"default";
    if (!iniSection)
    {
        iniSection = defaultIniSection;
    }

    DelayedWorker dworker(tmpdir, iniSection);
    IdleWorker iworker(tmpdir, iniSection);

    AwsS3 s3(tmpdir, iniSection, &dworker, &iworker);
    WinCse app(tmpdir, iniSection, &dworker, &iworker, &s3);

    return WinFspMain(argc, argv, progname, &app);
}

int wmain(int argc, wchar_t** argv)
{
#ifdef _DEBUG
    ::_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    std::wcout << L"Build: 2025/02/20 23:15 JST" << std::endl;

#if WINFSP_PASSTHROUGH
    std::wcout << L"Type: passthrough" << std::endl;
#else
    std::wcout << L"Type: WinCse" << std::endl;
#endif

#if _DEBUG
    std::wcout << L"Mode: Debug" << std::endl;
#else
    std::wcout << L"Mode: Release" << std::endl;
#endif

    WCHAR progname[] = L"WinCse";

    // メモリリーク調査を目的としてブロックを分ける
    int rc = EXIT_FAILURE;

    try
    {
        wchar_t* iniSection = nullptr;
        wchar_t* traceLogDir = nullptr;

        wchar_t** argp, ** arge;
        for (argp = argv + 1, arge = argv + argc; arge > argp; argp += 2)
        {
            if (L'-' != argp[0][0])
                break;

            switch (argp[0][1])
            {
                case L'S':
                    iniSection = *(argp + 1);
                    break;

                case L'T':
                    traceLogDir = *(argp + 1);
                    break;
            }
        }

        rc = app_main(argc, argv, progname, iniSection, traceLogDir);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << "what: " << err.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "unknown error" << std::endl;
    }

#ifdef _DEBUG
    ::_CrtDumpMemoryLeaks();
#endif

    return rc;
}

//
// 純粋仮想デストラクタの実装
//
IService::~IService() { };
ILogger::~ILogger() { }
ITask::~ITask() { }
IWorker::~IWorker() { }
IStorageService::~IStorageService() { }
ICloudStorage::~ICloudStorage() { }

//
void app_assert(const char* file, const int line, const char* func, const int signum)
{
    wchar_t tempPath[MAX_PATH];
    ::GetTempPathW(MAX_PATH, tempPath);

    const DWORD pid = ::GetCurrentProcessId();
    const DWORD tid = ::GetCurrentThreadId();

    std::wstring fpath;

    {
        std::wstringstream ss;
        ss << tempPath;
        ss << L"WinCse-abend-";
        ss << pid;
        ss << L'-';
        ss << tid;
        ss << L".log";

        fpath = ss.str();
    }

    std::ofstream ofs{ fpath, std::ios_base::app };

    {
        std::stringstream ss;

        ss << std::endl;
        ss << file;
        ss << "(";
        ss << line;
        ss << "); signum(";
        ss << signum;
        ss << "); ";
        ss << func;
        ss << std::endl;

        const std::string ss_str{ ss.str() };

        ::OutputDebugStringA(ss_str.c_str());

        if (ofs)
        {
            ofs << ss_str;
        }
    }

    const int maxFrames = 62;
    void* stack[maxFrames];
    HANDLE process = ::GetCurrentProcess();
    ::SymInitialize(process, NULL, TRUE);

    USHORT frames = ::CaptureStackBackTrace(0, maxFrames, stack, NULL);
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char));
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    for (USHORT i = 0; i < frames; i++)
    {
        ::SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);

        std::stringstream ss;
        ss << frames - i - 1;
        ss << ": ";
        ss << symbol->Name;
        ss << " - 0x";
        ss << symbol->Address;
        ss << std::endl;

        const std::string ss_str{ ss.str() };

        ::OutputDebugStringA(ss_str.c_str());

        std::cerr << ss_str;

        if (ofs)
        {
            ofs << ss_str;
        }
    }

    free(symbol);

    ofs.close();
}

static bool app_tempdir(std::wstring& wtmpdir)
{
    wchar_t tmpdir[MAX_PATH];
    const auto err = ::GetTempPath(MAX_PATH, tmpdir);
    APP_ASSERT(err != 0);

    if (tmpdir[wcslen(tmpdir) - 1] == L'\\')
    {
        tmpdir[wcslen(tmpdir) - 1] = L'\0';
    }

    wcscat_s(tmpdir, L"\\WinCse");

    if (!mkdirIfNotExists(tmpdir))
    {
        std::wcerr << tmpdir << L": dir not exists" << std::endl;
        return false;
    }
    wtmpdir = tmpdir;

    return true;
}

static void app_abort(int signum)
{
    app_assert(__FILE__, __LINE__, __FUNCTION__, signum);
}

static void app_terminate()
{
    app_assert(__FILE__, __LINE__, __FUNCTION__, -1);
}

ILogger* GetLogger()
{
    APP_ASSERT(gLogger);
    return gLogger;
}

// EOF