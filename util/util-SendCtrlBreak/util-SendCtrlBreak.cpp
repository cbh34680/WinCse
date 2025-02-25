#include <windows.h>
#include <iostream>

/*
* IDE からデバッグを開始後に IDE から停止させるとプロセスが
* 強制終了になってしまい、終了処理の調査ができない。
* このため、Ctrl+Break をプロセスに送信して WinFsp を通常停止させ
* 調査を可能とする。
* 
* この exe は util/debug-stop.bat から起動することを前提としている。
*/

static void ReAttachConout()
{
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
}

int main(int argc, char** argv)
{
    int ret = EXIT_FAILURE;

    DWORD processId = 0;
    HANDLE hProcess = NULL;

    if (argc > 1)
    {
        processId = (DWORD)atoi(argv[1]);
    }
    else
    {
        //std::cerr << "Usage: " << argv[0] << " {Process Id}" << std::endl;
        std::string input;
        std::cout << "Input ProcessId> ";
        std::cin >> input;

        processId = (DWORD)atoi(input.c_str());
    }

    if (processId < 1)
    {
        std::cerr << "Invalid Process Id: " << processId << std::endl;
        goto exit;
    }

    hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    if (hProcess == NULL) {
        std::cerr << "fault: OpenProcess: " << GetLastError() << std::endl;
        goto exit;
    }

    // 最初に現在のコンソールをデタッチしないと他のプロセスにアタッチできない
    if (!FreeConsole())
    {
        std::cerr << "fault: FreeConsole: " << GetLastError() << std::endl;
        goto exit;
    }

    // コンソールをアタッチ
    if (!AttachConsole(processId))
    {
        const auto err = GetLastError();
        ReAttachConout();
        std::cerr << "fault: AttachConsole: " << err << std::endl;
        goto exit;
    }

    // Ctrl+Break シグナルを送信
    if (!SetConsoleCtrlHandler(NULL, TRUE))
    {
        const auto err = GetLastError();
        ReAttachConout();
        std::cerr << "fault: SetConsoleCtrlHandler: " << err << std::endl;
        goto exit;
    }

    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0))
    {
        const auto err = GetLastError();
        ReAttachConout();
        std::cerr << "fault: GenerateConsoleCtrlEvent: " << err << std::endl;
        goto exit;
    }

    //ReAttachConout();

    //std::cout << "send [Ctrl+Break] to pid=" << processId << std::endl;
    ret = EXIT_SUCCESS;

exit:
    if (hProcess)
    {
        CloseHandle(hProcess);
    }

    return ret;
}

// EOF