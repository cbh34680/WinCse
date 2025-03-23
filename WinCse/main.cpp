// main.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#pragma comment(lib, "winfsp-x64.lib")
#pragma comment(lib, "WinCseLib.lib")


#include "WinCseLib.h"
#include "DelayedWorker.hpp"
#include "IdleWorker.hpp"
#include "WinCse.hpp"
#include <csignal>
#include <iostream>
#include <sstream>
#include <iomanip>

#define DIRECT_LINK_TEST        (0)

#if DIRECT_LINK_TEST
#pragma comment(lib, "WinCse-aws-s3.lib")
#include "..\WinCse-aws-s3\\AwsS3.hpp"
#endif


using namespace WinCseLib;

static WCHAR PROGNAME[] = L"WinCse";

static bool app_tempdir(std::wstring* tmpDir);
static void app_terminate();
static void app_sighandler(int signum);

static void writeStats(
    const wchar_t* logDir, const WINFSP_STATS* libStats,
    const WINCSE_DRIVER_STATS* appStats, const WINCSE_DEVICE_STATS* devStats);

/*
 * DEBUG ARGS

    [nolog]
        -u \WinCse.aws-s3.Y\C$\$(MSBuildProjectDirectoryNoRoot)\..\..\MOUNT -m Y:

    [with log]
        -u \WinCse.aws-s3.Y\C$\$(MSBuildProjectDirectoryNoRoot)\..\..\MOUNT -m Y: -T $(SolutionDir)\trace
*/

// DLL 解放のための RAII
struct DllModuleRAII
{
    HMODULE mModule;
    ICSDevice* mCSDevice;

    DllModuleRAII() : mModule(NULL), mCSDevice(nullptr) {}
    DllModuleRAII(HMODULE argModule, ICSDevice* argCSDevice) : mModule(argModule), mCSDevice(argCSDevice) {}

    ~DllModuleRAII()
    {
        delete mCSDevice;

        if (mModule)
        {
            ::FreeLibrary(mModule);
        }
    }
};

#if DIRECT_LINK_TEST
bool loadCSDevice(const std::wstring& dllType,
    const std::wstring& tmpDir, const wchar_t* iniSection,
    WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker,
    DllModuleRAII* pDll)
{
    pDll->mCSDevice = NewCSDevice(tmpDir.c_str(), iniSection, delayedWorker, idleWorker);

    return true;
}

#else
//
// 引数の名前 (dllType) からファイル名を作り、LoadLibrary を実行し
// その中にエクスポートされた NewCSDevice() を実行する。
// 戻り値は ICSDevice* になる。
//
bool loadCSDevice(const std::wstring& dllType,
    const std::wstring& tmpDir, const wchar_t* iniSection,
	WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker,
    DllModuleRAII* pDll)
{
    NEW_LOG_BLOCK();

    bool ret = false;

	const std::wstring dllName{ std::wstring(PROGNAME) + L'-' + dllType + L".dll" };

	typedef WinCseLib::ICSDevice* (*NewCSDevice)(
		const wchar_t* argTempDir, const wchar_t* argIniSection,
		WinCseLib::IWorker * delayedWorker, WinCseLib::IWorker * idleWorker);

	NewCSDevice dllFunc = nullptr;
    ICSDevice* pCSDevice = nullptr;

	HMODULE hModule = ::LoadLibrary(dllName.c_str());
	if (hModule == NULL)
	{
        std::wcerr << L"fault: LoadLibrary" << dllName << std::endl;
        traceW(L"fault: LoadLibrary %s", dllName.c_str());
        goto exit;
	}
    
    dllFunc = (NewCSDevice)::GetProcAddress(hModule, "NewCSDevice");
	if (!dllFunc)
	{
        std::wcerr << L"fault: GetProcAddress" << std::endl;
        traceW(L"fault: GetProcAddress");
        goto exit;
    }

    pCSDevice = dllFunc(tmpDir.c_str(), iniSection, delayedWorker, idleWorker);
	if (!pCSDevice)
	{
        std::wcerr << L"fault: NewCSDevice" << std::endl;
        traceW(L"fault: NewCSDevice");
        goto exit;
    }

    pDll->mModule = hModule;
    pDll->mCSDevice = pCSDevice;

    hModule = NULL;
    pCSDevice = nullptr;

    traceW(L"success");

    ret = true;

exit:
    delete pCSDevice;

    if (hModule)
    {
        ::FreeLibrary(hModule);
    }

    return ret;
}

#endif

static int app_main(int argc, wchar_t** argv,
    const wchar_t* iniSection, const wchar_t* trcDir, const std::wstring& dllType)
{
    std::signal(SIGABRT, app_sighandler);

    // スレッドでの捕捉されない例外を拾えるかも
    std::set_terminate(app_terminate);

    std::wstring tmpDir;
    if (!app_tempdir(&tmpDir))
    {
        std::cerr << "fault: app_tempdir" << std::endl;
        return EXIT_FAILURE;
    }

    std::wcout << L"use Tempdir: " << tmpDir << std::endl;

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
    if (CreateLogger(tmpDir.c_str(), trcDir, dllType.c_str()))
    {
        // traceW/A が使えるのはここから

        NEW_LOG_BLOCK();

        {
            // メモリ解放の順番が関係するので、下の try ブロックには入れない
            DllModuleRAII dll;

            try
            {
                wchar_t defaultIniSection[] = L"default";
                if (!iniSection)
                {
                    std::wcout << L"use default ini section" << std::endl;
                    traceW(L"use default ini section");
                    iniSection = defaultIniSection;
                }

                std::wcout << L"iniSection: " << iniSection << std::endl;
                traceW(L"iniSection: %s", iniSection);

                DelayedWorker dworker(tmpDir, iniSection);
                IdleWorker iworker(tmpDir, iniSection);

                std::wcout << L"load dll type=" << dllType << std::endl;
                traceW(L"load dll type=%s", dllType.c_str());

                // dll のロード
                if (loadCSDevice(dllType, tmpDir, iniSection, &dworker, &iworker, &dll))
                {
                    // WinCse メンバのデストラクタでの処理を appSTats に反映させるため
                    // app の生存期間より長くする

                    WINCSE_DRIVER_STATS appStats{};
                    WINFSP_IF libif{};

                    {
                        WinCse app(&appStats, tmpDir, iniSection, &dworker, &iworker, dll.mCSDevice);

                        std::wcout << L"call WinFspMain" << std::endl;
                        traceW(L"call WinFspMain");

                        libif.pDriver = &app;

                        rc = WinFspMain(argc, argv, PROGNAME, &libif);
                    }

                    const wchar_t* logDir = GetLogger()->getOutputDirectory();
                    if (logDir)
                    {
                        WINCSE_DEVICE_STATS devStats{};
                        dll.mCSDevice->queryStats(&devStats);

                        writeStats(logDir, &libif.stats, &appStats, &devStats);
                    }

                    std::wcout << L"WinFspMain done. return=" << rc << std::endl;
                    traceW(L"WinFspMain done. return=%d", rc);
                }
                else
                {
                    std::wcerr << L"fault: loadCSDevice" << std::endl;
                    traceW(L"fault: loadCSDevice");
                }
            }
            catch (const std::exception& ex)
            {
                std::cerr << "catch exception: what=" << ex.what() << std::endl;
                traceA("catch exception: what=%s", ex.what());
            }
            catch (...)
            {
                std::cerr << "catch exception: unknown" << std::endl;
                traceA("catch exception: unknown");
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

#ifdef _DEBUG
    ::_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    std::wcout << L"Build: 2025/02/25 16:30 JST" << std::endl;

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
        const auto names{ SplitString(segments[0], L'.', false) };
        if (names.size() < 2)
        {
            std::wcerr << L"[u] parameter parse error" << std::endl;
            return EXIT_FAILURE;
        }

        rc = app_main(argc, argv, iniSection, traceLogDir, names[1]);
    }
    catch (const std::exception& err)
    {
        std::cerr << "wmain) what: " << err.what() << std::endl;
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

static void app_sighandler(int signum)
{
    WinCseLib::AbnormalEnd(__FILE__, __LINE__, __FUNCTION__, signum);
}

static void app_terminate()
{
    WinCseLib::AbnormalEnd(__FILE__, __LINE__, __FUNCTION__, -1);
}

static void writeStats(
    const wchar_t* logDir, const WINFSP_STATS* libStats,
    const WINCSE_DRIVER_STATS* appStats, const WINCSE_DEVICE_STATS* devStats)
{
    SYSTEMTIME st;
    ::GetLocalTime(&st);

    std::wstringstream ss;
    ss << logDir;
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

    const std::wstring path{ ss.str() };

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wt") == 0)
    {
        if (fp)
        {
            fprintf(fp, "Main Thread: %ld\n", ::GetCurrentThreadId());
            fputs("\n", fp);

            fputs("[WinFsp Stats]\n", fp);
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
            fprintf(fp, "\t" "PreCreateFilesystem: %ld\n", appStats->PreCreateFilesystem);
            fprintf(fp, "\t" "OnSvcStart: %ld\n", appStats->OnSvcStart);
            fprintf(fp, "\t" "OnSvcStop: %ld\n", appStats->OnSvcStop);
            fprintf(fp, "\t" "DoGetSecurityByName: %ld\n", appStats->DoGetSecurityByName);
            fprintf(fp, "\t" "DoGetFileInfo: %ld\n", appStats->DoGetFileInfo);
            fprintf(fp, "\t" "DoGetSecurity: %ld\n", appStats->DoGetSecurity);
            fprintf(fp, "\t" "DoGetVolumeInfo: %ld\n", appStats->DoGetVolumeInfo);
            fprintf(fp, "\t" "DoCreate: %ld\n", appStats->DoCreate);
            fprintf(fp, "\t" "DoOpen: %ld\n", appStats->DoOpen);
            fprintf(fp, "\t" "DoCleanup: %ld\n", appStats->DoCleanup);
            fprintf(fp, "\t" "DoClose: %ld\n", appStats->DoClose);
            fprintf(fp, "\t" "DoRead: %ld\n", appStats->DoRead);
            fprintf(fp, "\t" "DoWrite: %ld\n", appStats->DoWrite);
            fprintf(fp, "\t" "DoReadDirectory: %ld\n", appStats->DoReadDirectory);
            fprintf(fp, "\t" "DoFlush: %ld\n", appStats->DoFlush);
            fprintf(fp, "\t" "DoOverwrite: %ld\n", appStats->DoOverwrite);
            fprintf(fp, "\t" "DoRename: %ld\n", appStats->DoRename);
            fprintf(fp, "\t" "DoSetBasicInfo: %ld\n", appStats->DoSetBasicInfo);
            fprintf(fp, "\t" "DoSetDelete: %ld\n", appStats->DoSetDelete);
            fprintf(fp, "\t" "DoSetFileSize: %ld\n", appStats->DoSetFileSize);
            fprintf(fp, "\t" "DoSetPath: %ld\n", appStats->DoSetPath);
            fprintf(fp, "\t" "DoSetSecurity: %ld\n", appStats->DoSetSecurity);

            fprintf(fp, "\t" "_CallCreate: %ld\n", appStats->_CallCreate);
            fprintf(fp, "\t" "_CallOpen: %ld\n", appStats->_CallOpen);
            fprintf(fp, "\t" "_CallClose: %ld\n", appStats->_CallClose);
            fprintf(fp, "\t" "_ForceClose: %ld\n", appStats->_ForceClose);
            fputs("\n", fp);

            fputs("[CSDevice Stats]\n", fp);
            fprintf(fp, "\t" "OnSvcStart: %ld\n", devStats->OnSvcStart);
            fprintf(fp, "\t" "OnSvcStop: %ld\n", devStats->OnSvcStop);
            fprintf(fp, "\t" "headBucket: %ld\n", devStats->headBucket);
            fprintf(fp, "\t" "headObject: %ld\n", devStats->headObject);
            fprintf(fp, "\t" "listBuckets: %ld\n", devStats->listBuckets);
            fprintf(fp, "\t" "listObjects: %ld\n", devStats->listObjects);
            fprintf(fp, "\t" "create: %ld\n", devStats->create);
            fprintf(fp, "\t" "open: %ld\n", devStats->open);
            fprintf(fp, "\t" "cleanup: %ld\n", devStats->cleanup);
            fprintf(fp, "\t" "close: %ld\n", devStats->close);
            fprintf(fp, "\t" "readObject: %ld\n", devStats->readObject);
            fprintf(fp, "\t" "writeObject: %ld\n", devStats->writeObject);
            fprintf(fp, "\t" "remove: %ld\n", devStats->remove);

            fputs("\n", fp);

            fclose(fp);
        }
    }
}

// EOF