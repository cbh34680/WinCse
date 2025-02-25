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

using namespace WinCseLib;

static WCHAR PROGNAME[] = L"WinCse";

static bool app_tempdir(std::wstring* tmpDir);
static void app_terminate();
static void app_sighandler(int signum);

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
    ICloudStorage* mStorage;

    DllModule() : mModule(NULL), mStorage(nullptr) {}
    DllModule(HMODULE mod, ICloudStorage* storage) : mModule(mod), mStorage(storage) {}

    ~DllModule()
    {
        delete mStorage;

        if (mModule)
        {
            ::FreeLibrary(mModule);
        }
    }
};

//
// 引数の名前 (dllType) からファイル名を作り、LoadLibrary を実行し
// その中にエクスポートされた NewCloudStorage() を実行する。
// 戻り値は ICloudStorage* になる。
//
bool loadCloudStorage(const std::wstring& dllType,
    const std::wstring& tmpDir, const wchar_t* iniSection,
	WinCseLib::IWorker* delayedWorker, WinCseLib::IWorker* idleWorker,
    DllModule* pDll)
{
    NEW_LOG_BLOCK();

    bool ret = false;

	const std::wstring dllName{ std::wstring(PROGNAME) + L'-' + dllType + L".dll" };

	typedef WinCseLib::ICloudStorage* (*NewCloudStorage)(
		const wchar_t* argTempDir, const wchar_t* argIniSection,
		WinCseLib::IWorker * delayedWorker, WinCseLib::IWorker * idleWorker);

	NewCloudStorage dllFunc = nullptr;
    ICloudStorage* pStorage = nullptr;

	HMODULE hMod = ::LoadLibrary(dllName.c_str());
	if (hMod == NULL)
	{
        std::wcerr << L"fault: LoadLibrary" << dllName << std::endl;
        traceW(L"fault: LoadLibrary %s", dllName.c_str());
        goto exit;
	}
    
    dllFunc = (NewCloudStorage)::GetProcAddress(hMod, "NewCloudStorage");
	if (!dllFunc)
	{
        std::wcerr << L"fault: GetProcAddress" << std::endl;
        traceW(L"fault: GetProcAddress");
        goto exit;
    }

    pStorage = dllFunc(tmpDir.c_str(), iniSection, delayedWorker, idleWorker);
	if (!pStorage)
	{
        std::wcerr << L"fault: NewCloudStorage" << std::endl;
        traceW(L"fault: NewCloudStorage");
        goto exit;
    }

    pDll->mModule = hMod;
    pDll->mStorage = pStorage;

    hMod = NULL;
    pStorage = nullptr;

    traceW(L"success");

    ret = true;

exit:
    delete pStorage;

    if (hMod)
    {
        ::FreeLibrary(hMod);
    }

    return ret;
}

static int app_main(int argc, wchar_t** argv,
    const wchar_t* iniSection, const wchar_t* trcDir, const std::wstring& dllType)
{
    // これやらないと日本語が出力できない
    _wsetlocale(LC_ALL, L"");

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
    int ret = EXIT_FAILURE;

    //
    // ネストが深くなっているが、dll の解放と logger が複雑に絡むので
    // このブロックの構造は変更しないこと
    // 
    // [順番]
    // 1) ロガー生成 (CreateLogger)
    // 2) ワーカー生成
    // 3) DLL ロード & CloudStorage の取得
    // 4) WinFspMain の実行
    // 5) ワーカー解放 (スタックによる自動)
    // 6) DLL アンロード (DllModule デストラクタ)
    // 7) ロガー解放 (DeleteLogger)
    //
    if (CreateLogger(tmpDir.c_str(), trcDir, dllType.c_str()))
    {
        NEW_LOG_BLOCK();

        // メモリ解放の順番が関係するので、下の try ブロックには入れない
        DllModule dll;

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
            if (loadCloudStorage(dllType, tmpDir, iniSection, &dworker, &iworker, &dll))
            {
                WinCse app(tmpDir, iniSection, &dworker, &iworker, dll.mStorage);

                std::wcout << L"call WinFspMain" << std::endl;
                traceW(L"call WinFspMain");

                ret = WinFspMain(argc, argv, PROGNAME, &app);

                std::wcout << L"WinFspMain done. return=" << ret << std::endl;
                traceW(L"WinFspMain done. return=%s", ret ? L"true" : L"false");
            }
            else
            {
                std::wcerr << L"fault: loadCloudStorage" << std::endl;
                traceW(L"fault: loadCloudStorage");
            }
        }
        catch (const std::runtime_error& err)
        {
            std::cerr << "app_main) what: " << err.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "app_main) unknown error" << std::endl;
        }
    }
    else
    {
        std::cerr << "fault: CreateLogger" << std::endl;
        return EXIT_FAILURE;
    }

    // 順番があるので、try ブロックには入れない
    DeleteLogger();

    return ret;
}

int wmain(int argc, wchar_t** argv)
{
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
        const auto segments{ SplitW(VolumePrefix, L'\\', true) };
        if (segments.size() < 1)
        {
            std::wcerr << L"[u] parameter parse error" << std::endl;
            return EXIT_FAILURE;
        }

        // "WinCse.aws-s3.Y" の中から "aws-s3" を取り出す
        const auto names{ SplitW(segments[0], L'.', false) };
        if (names.size() < 2)
        {
            std::wcerr << L"[u] parameter parse error" << std::endl;
            return EXIT_FAILURE;
        }

        rc = app_main(argc, argv, iniSection, traceLogDir, names[1]);
    }
    catch (const std::runtime_error& err)
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

// EOF