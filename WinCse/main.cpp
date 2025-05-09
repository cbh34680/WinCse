// main.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#pragma comment(lib, "winfsp-x64.lib")
#pragma comment(lib, "WinCseLib.lib")


#include "WinCseLib.h"
#include "CSDriver.hpp"
#include "DelayedWorker.hpp"
#include "TimerWorker.hpp"
#include <csignal>
#include <iostream>
#include <iomanip>

#define DIRECT_LINK_TEST        (0)

#if DIRECT_LINK_TEST
#pragma comment(lib, "WinCse-aws-s3.lib")
#include "..\WinCse-aws-s3\CSDevice.hpp"
#endif


using namespace CSELIB;
using namespace CSEDRV;


static WCHAR PROGNAME[] = L"WinCse";

static void app_terminate();
static void app_sighandler(int signum);

static WCHAR WINCSE_BUILD_TIME[] = L"Build: 2025/05/09 18:10 JST";
static WCHAR AWS_SDK_CPP_COMMIT[] = L"aws-sdk-cpp: Commit 6b03639";

static void writeStats(
    PCWSTR logDir, const WINFSP_STATS* libStats,
    const WINCSE_DRIVER_STATS* appStats);

/*
 * DEBUG ARGS

    [nolog]
        -u \WinCse.aws-s3.Y\C$\$(MSBuildProjectDirectoryNoRoot)\..\..\MOUNT -m Y:

    [with log]
        -u \WinCse.aws-s3.Y\C$\$(MSBuildProjectDirectoryNoRoot)\..\..\MOUNT -m Y: -T $(SolutionDir)\trace
*/

// DLL 解放のための RAII

struct DllModule
{
    HMODULE mModule;
    ICSDevice* mDevice;

    DllModule()
        :
        mModule(NULL),
        mDevice(nullptr)
    {
    }

    DllModule(HMODULE argModule, ICSDevice* argCSDevice)
        :
        mModule(argModule),
        mDevice(argCSDevice)
    {
    }

    ~DllModule()
    {
        delete mDevice;

        if (mModule)
        {
            ::FreeLibrary(mModule);
        }
    }
};

#if DIRECT_LINK_TEST
static bool loadCSDevice(const std::wstring&, PCWSTR iniSection, NamedWorker workers[], DllModule* pDll)
{
    pDll->mDevice = NewCSDevice(iniSection, workers);

    return true;
}

#else
//
// 引数の名前 (dllType) からファイル名を作り、LoadLibrary を実行し
// その中にエクスポートされた NewCSDevice() を実行する。
// 戻り値は ICSDevice* になる。
//
static bool loadCSDevice(const std::wstring& csDeviceType, PCWSTR iniSection, NamedWorker workers[], DllModule* pDll)
{
    NEW_LOG_BLOCK();

    bool ret = false;

	const auto dllName{ std::wstring(PROGNAME) + L'-' + csDeviceType + L".dll" };

    using NewCSDevice = ICSDevice* (*)(PCWSTR argIniSection, NamedWorker workers[]);

	NewCSDevice dllFunc = nullptr;
    ICSDevice* pDevice = nullptr;

	HMODULE hModule = ::LoadLibrary(dllName.c_str());
	if (hModule == NULL)
	{
        std::wcerr << L"fault: LoadLibrary" << dllName << std::endl;
        errorW(L"fault: LoadLibrary %s", dllName.c_str());
        goto exit;
	}
    
    dllFunc = (NewCSDevice)::GetProcAddress(hModule, "NewCSDevice");
	if (!dllFunc)
	{
        std::wcerr << L"fault: GetProcAddress" << std::endl;
        errorW(L"fault: GetProcAddress");
        goto exit;
    }

    pDevice = dllFunc(iniSection, workers);
	if (!pDevice)
	{
        std::wcerr << L"fault: NewCSDevice" << std::endl;
        errorW(L"fault: NewCSDevice");
        goto exit;
    }

    pDll->mModule = hModule;
    pDll->mDevice = pDevice;

    hModule = NULL;
    pDevice = nullptr;

    traceW(L"success");

    ret = true;

exit:
    delete pDevice;

    if (hModule)
    {
        ::FreeLibrary(hModule);
    }

    return ret;
}

#endif

static int app_main(int argc, wchar_t** argv, PCWSTR iniSection, PCWSTR traceLogDir, const std::wstring& csDeviceType)
{
    std::signal(SIGABRT, app_sighandler);

    // スレッドでの捕捉されない例外を拾えるかも

    std::set_terminate(app_terminate);

    // ここ以降は return は使わず、ret に設定して最後まで進める

    int rc = EXIT_FAILURE;

    //
    // ネストが深くなっているが、dll の解放と logger が複雑に絡むので
    // このブロックの構造は変更しないこと
    // 
    // [順番]
    // 1) ロガー生成 (CreateLogger)
    // 2) ワーカー生成
    // 3) DLL ロード & CSDevice の取得
    // 4) WinFspMain の実行
    // 5) ワーカー解放 (スタックによる自動)
    // 6) DLL アンロード (DllModuleRAII デストラクタ)
    // 7) ロガー解放 (DeleteLogger)
    //
    if (CreateLogger(traceLogDir))
    {
        // traceW/A が使えるのはここから

        NEW_LOG_BLOCK();

        {
            // メモリ解放の順番が関係するので、下の try ブロックには入れない

            DllModule dll;

            try
            {
                wchar_t defaultIniSection[] = L"default";
                if (!iniSection)
                {
                    std::wcout << L"use 'default' ini section" << std::endl;
                    traceW(L"use 'default' ini section");
                    iniSection = defaultIniSection;
                }

                std::wcout << L"iniSection: " << iniSection << std::endl;
                traceW(L"iniSection: %s", iniSection);

                DelayedWorker dworker(iniSection);
                TimerWorker tworker(iniSection);

                NamedWorker workers[] =
                {
                    { L"delayed", &dworker },
                    { L"timer", &tworker },
                    { nullptr, nullptr },
                };

                std::wcout << L"load dll type=" << csDeviceType << std::endl;
                traceW(L"load dll type=%s", csDeviceType.c_str());

                // dll のロード

                if (loadCSDevice(csDeviceType, iniSection, workers, &dll))
                {
                    // WinCse メンバのデストラクタでの処理を appSTats に反映させるため
                    // app の生存期間より長くする

                    WINCSE_DRIVER_STATS appStats{};
                    WINCSE_IF appif{};

                    {
                        // このブロック化は必要

                        std::wcout << L"call NewCSDriver" << std::endl;
                        traceW(L"call NewCSDriver");

                        auto app{ std::unique_ptr<ICSDriver>{ NewCSDriver(csDeviceType.c_str(), iniSection, workers, dll.mDevice, &appStats) } };
                        if (app)
                        {
                            appif.mDriver = app.get();

                            rc = WinFspMain(argc, argv, PROGNAME, &appif);
                        }
                        else
                        {
                            std::wcerr << L"fault: NewCSDriver" << std::endl;
                            errorW(L"fault: NewCSDriver");
                        }
                    }

                    PCWSTR outputDir = GetLogger()->getOutputDirectory();
                    if (outputDir)
                    {
                        writeStats(outputDir, &appif.FspStats, &appStats);
                    }

                    std::wcout << L"WinFspMain done. return=" << rc << std::endl;
                    traceW(L"WinFspMain done. return=%d", rc);
                }
                else
                {
                    std::wcerr << L"fault: loadCSDevice" << std::endl;
                    errorW(L"fault: loadCSDevice");
                }
            }
            catch (const std::exception& ex)
            {
                std::cerr << "catch exception: what=" << ex.what() << std::endl;
                errorA("catch exception: what=%s", ex.what());
            }
            catch (...)
            {
                std::cerr << "catch exception: unknown" << std::endl;
                errorA("catch exception: unknown");
            }
        }

        std::wcout << L"all done." << std::endl;
        traceW(L"all done.");
    }
    else
    {
        std::cerr << "fault: CreateLogger" << std::endl;
        return EXIT_FAILURE;
    }

    // 順番があるので、try ブロックには入れない

    DeleteLogger();

    return rc;
}

int wmain(int argc, wchar_t** argv)
{
    std::locale::global(std::locale("", LC_ALL));
    std::wcout.imbue(std::locale("", LC_ALL));
    std::wcerr.imbue(std::locale("", LC_ALL));

    // これやらないと日本語が出力できない
    _wsetlocale(LC_ALL, L"");
    setlocale(LC_ALL, "");
    ::SetConsoleOutputCP(CP_UTF8);

    std::wcout << L"[External Libraries]" << std::endl;
    std::wcout << L"WinFsp: 2.0.23075" << std::endl;
    std::wcout << AWS_SDK_CPP_COMMIT << std::endl;

#ifdef _DEBUG
    ::_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    std::wcout << L"[WinCse]" << std::endl;
    std::wcout << WINCSE_BUILD_TIME << std::endl;

#if WINFSP_PASSTHROUGH
    std::wcout << L"Type: passthrough" << std::endl;
#else
    std::wcout << L"Type: WinCse" << std::endl;
#endif

#ifdef _DEBUG
    std::wcout << L"Mode: Debug" << std::endl;
#else
    std::wcout << L"Mode: Release" << std::endl;
#endif

    std::wcout << L"argv: ";
    for (int i=0; i<argc; i++)
    {
        if (i != 0)
        {
            std::wcout << L' ';
        }

        std::wcout << L'"' << argv[i] << L'"';
    }
    std::wcout << std::endl;

    const DWORD pid= ::GetCurrentProcessId();
    std::wcout << L"ProcessId: " << pid << std::endl;

    // メモリリーク調査を目的としてブロックを分ける
    int rc = EXIT_FAILURE;

    try
    {
        wchar_t* iniSection = nullptr;
        wchar_t* traceLogDir = nullptr;
        wchar_t* VolumePrefix = nullptr;

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

                case L'u':
                    VolumePrefix = *(argp + 1);
                    break;
            }
        }

        if (!VolumePrefix)
        {
            // 本来は任意項目かもしれないが、ここから dll 名を解決するので
            // ここでは必須とする

            std::wcerr << L"[u] parameter not set" << std::endl;
            return EXIT_FAILURE;
        }

        // "\WinCse.aws-s3.Y\C$\folder\to\work" から "WinCse.aws-s3.Y" を取り出す
        const auto segments{ SplitString(VolumePrefix, L'\\', true) };
        if (segments.size() < 1)
        {
            std::wcerr << L"[u] parameter parse error" << std::endl;
            return EXIT_FAILURE;
        }

        // "WinCse.aws-s3.Y" の中から "aws-s3" を取り出す
        const auto names{ SplitString(segments[0], L'.', true) };
        if (names.size() < 2)
        {
            std::wcerr << L"[u] parameter parse error" << std::endl;
            return EXIT_FAILURE;
        }

        rc = app_main(argc, argv, iniSection, traceLogDir, names[1]);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "wmain) what: " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "wmain) unknown error" << std::endl;
    }

#ifdef _DEBUG
    ::_CrtDumpMemoryLeaks();
#endif

    return rc;
}

static void app_sighandler(int signum)
{
    AbnormalEnd(__FILEW__, __LINE__, __FUNCTIONW__, signum);
}

static void app_terminate()
{
    AbnormalEnd(__FILEW__, __LINE__, __FUNCTIONW__, -1);
}

static void writeStats(PCWSTR outputDir, const WINFSP_STATS* libStats, const WINCSE_DRIVER_STATS* appStats)
{
    SYSTEMTIME st;
    ::GetLocalTime(&st);

    std::wostringstream ss;

    ss << outputDir;
    ss << L'\\';
    ss << L"stats";
    ss << L'-';
    ss << std::setw(4) << std::setfill(L'0') << st.wYear;
    ss << std::setw(2) << std::setfill(L'0') << st.wMonth;
    ss << std::setw(2) << std::setfill(L'0') << st.wDay;
    ss << L'-';
    ss << std::setw(2) << std::setfill(L'0') << st.wHour;
    ss << std::setw(2) << std::setfill(L'0') << st.wMinute;
    ss << std::setw(2) << std::setfill(L'0') << st.wSecond;
    ss << L".log";

    const auto path{ ss.str() };

    FILE* fp = nullptr;

    if (_wfopen_s(&fp, path.c_str(), L"wt") == 0)
    {
        if (fp)
        {
            fprintf(fp, "Main Thread: %ld\n", ::GetCurrentThreadId());
            fputs("\n", fp);

            fputs("[WinFsp Stats]\n", fp);
            fprintf(fp, "\t" "SvcStart: %ld\n", libStats->SvcStart);
            fprintf(fp, "\t" "SvcStop: %ld\n", libStats->SvcStop);
            fprintf(fp, "\t" "GetSecurityByName: %ld\n", libStats->GetSecurityByName);
            fprintf(fp, "\t" "GetFileInfo: %ld\n", libStats->GetFileInfo);
            fprintf(fp, "\t" "GetFileInfoInternal: %ld\n", libStats->GetFileInfoInternal);
            fprintf(fp, "\t" "GetSecurity: %ld\n", libStats->GetSecurity);
            fprintf(fp, "\t" "GetVolumeInfo: %ld\n", libStats->GetVolumeInfo);
            fprintf(fp, "\t" "Create: %ld\n", libStats->Create);
            fprintf(fp, "\t" "Open: %ld\n", libStats->Open);
            fprintf(fp, "\t" "Cleanup: %ld\n", libStats->Cleanup);
            fprintf(fp, "\t" "Close: %ld\n", libStats->Close);
            fprintf(fp, "\t" "Read: %ld\n", libStats->Read);
            fprintf(fp, "\t" "Write: %ld\n", libStats->Write);
            fprintf(fp, "\t" "ReadDirectory: %ld\n", libStats->ReadDirectory);
            fprintf(fp, "\t" "Flush: %ld\n", libStats->Flush);
            fprintf(fp, "\t" "Overwrite: %ld\n", libStats->Overwrite);
            fprintf(fp, "\t" "Rename: %ld\n", libStats->Rename);
            fprintf(fp, "\t" "SetBasicInfo: %ld\n", libStats->SetBasicInfo);
            fprintf(fp, "\t" "SetDelete: %ld\n", libStats->SetDelete);
            fprintf(fp, "\t" "SetFileSize: %ld\n", libStats->SetFileSize);
            fprintf(fp, "\t" "SetSecurity: %ld\n", libStats->SetSecurity);
            fprintf(fp, "\t" "SetVolumeLabel_: %ld\n", libStats->SetVolumeLabel_);
            fputs("\n", fp);

            fputs("[CSDriver Stats]\n", fp);
            fprintf(fp, "\t" "RelayPreCreateFilesystem: %ld\n", appStats->RelayPreCreateFilesystem);
            fprintf(fp, "\t" "RelayOnSvcStart: %ld\n", appStats->RelayOnSvcStart);
            fprintf(fp, "\t" "RelayOnSvcStop: %ld\n", appStats->RelayOnSvcStop);
            fprintf(fp, "\t" "RelayGetSecurityByName: %ld\n", appStats->RelayGetSecurityByName);
            fprintf(fp, "\t" "RelayGetFileInfo: %ld\n", appStats->RelayGetFileInfo);
            fprintf(fp, "\t" "RelayGetSecurity: %ld\n", appStats->RelayGetSecurity);
            fprintf(fp, "\t" "RelayCreate: %ld\n", appStats->RelayCreate);
            fprintf(fp, "\t" "RelayOpen: %ld\n", appStats->RelayOpen);
            fprintf(fp, "\t" "RelayCleanup: %ld\n", appStats->RelayCleanup);
            fprintf(fp, "\t" "RelayClose: %ld\n", appStats->RelayClose);
            fprintf(fp, "\t" "RelayRead: %ld\n", appStats->RelayRead);
            fprintf(fp, "\t" "RelayWrite: %ld\n", appStats->RelayWrite);
            fprintf(fp, "\t" "RelayReadDirectory: %ld\n", appStats->RelayReadDirectory);
            fprintf(fp, "\t" "RelayFlush: %ld\n", appStats->RelayFlush);
            fprintf(fp, "\t" "RelayOverwrite: %ld\n", appStats->RelayOverwrite);
            fprintf(fp, "\t" "RelayRename: %ld\n", appStats->RelayRename);
            fprintf(fp, "\t" "RelaySetBasicInfo: %ld\n", appStats->RelaySetBasicInfo);
            fprintf(fp, "\t" "RelaySetDelete: %ld\n", appStats->RelaySetDelete);
            fprintf(fp, "\t" "RelaySetFileSize: %ld\n", appStats->RelaySetFileSize);
            fprintf(fp, "\t" "RelaySetSecurity: %ld\n", appStats->RelaySetSecurity);
            fputs("\n", fp);

            fclose(fp);
        }
    }
}

// EOF