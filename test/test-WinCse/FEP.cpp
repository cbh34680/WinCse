#include "WinCseLib.h"
#include "CSDevice.hpp"
#include <iostream>

#pragma comment(lib, "WinCse-aws-s3.lib")

using namespace CSELIB;


struct NoopWorker : public IWorker
{
    bool addTask(ITask* argTask)
    {
        argTask->cancelled();
        delete argTask;

        return true;
    }
};

void t_WinCseLib_aws_s3_FEP(std::initializer_list<std::function<void(ICSDevice*)>> callbacks)
{
    if (!CreateLogger(L"Q:\\not-exists\\dir"))
    {
        std::wcerr << L"fault: CreateLogger" << std::endl;
        return;
    }

    wchar_t *workdir = nullptr;
    size_t len;
    errno_t err = _wdupenv_s(&workdir, &len, L"WINCSE_TEST_WORK_DIR");
    if (err)
    {
        std::wcerr << L"fault: _wdupenv_s" << std::endl;
        return;
    }

    if (!workdir)
    {
        std::wcerr << L"fault: workdir is null" << std::endl;
        return;
    }

    PCWSTR iniSection = L"default";

    NoopWorker noop;

    NamedWorker workers[] =
    {
        { L"delayed", &noop },
        { L"timer", &noop },
        { nullptr, nullptr },
    };

    auto* device = NewCSDevice(iniSection, workers);
    if (!device)
    {
        std::wcerr << L"fault: NewCSDevice" << std::endl;
        return;
    }

    FSP_FSCTL_VOLUME_PARAMS vp{};

    auto ntstatus = device->PreCreateFilesystem(nullptr, workdir, &vp);
    if (!NT_SUCCESS(ntstatus))
    {
        std::wcerr << L"fault: PreCreateFilesystem" << std::endl;
        return;
    }

    ntstatus = device->OnSvcStart(workdir, nullptr);
    if (!NT_SUCCESS(ntstatus))
    {
        std::wcerr << L"fault: OnSvcStart" << std::endl;
        return;
    }

    for (const auto& callback: callbacks)
    {
        callback(device);
    }

    device->OnSvcStop();

    delete device;
    free(workdir);

    DeleteLogger();

    std::wcout << L"done." << std::endl;
}

// EOF