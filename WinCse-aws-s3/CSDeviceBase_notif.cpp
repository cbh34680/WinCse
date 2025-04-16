#include "CSDeviceBase.hpp"

using namespace WCSE;


static std::atomic<bool> gEndWorkerFlag;
static std::thread* gNotifWorker;

static HANDLE gNotifEvents[2];
static PCWSTR gEventNames[] =
{
    L"Global\\WinCse-AwsS3-util-print-report",
    L"Global\\WinCse-AwsS3-util-clear-cache",
};

static const int gNumNotifEvents = _countof(gNotifEvents);

bool CSDeviceBase::createNotifListener(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);

    // セキュリティ記述子の作成

    SECURITY_DESCRIPTOR sd{};
    if (!::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
    {
        traceW(L"fault: InitializeSecurityDescriptor");
        return false;
    }

    // すべてのユーザーにフルアクセスを許可
#pragma warning(suppress: 6248)
    if (!::SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE))
    {
        traceW(L"fault: SetSecurityDescriptorDacl");
        return false;
    }

    sa.lpSecurityDescriptor = &sd;

    static_assert(_countof(gEventNames) == gNumNotifEvents);

    for (int i=0; i<gNumNotifEvents; i++)
    {
        gNotifEvents[i] = ::CreateEventW(&sa, FALSE, FALSE, gEventNames[i]);
        if (!gNotifEvents[i])
        {
            const auto lerr = ::GetLastError();
            traceW(L"fault: CreateEvent(%s) error=%ld", gEventNames[i], lerr);
            return false;
        }
    }

    gNotifWorker = new std::thread(&CSDeviceBase::notifListener, this);
    APP_ASSERT(gNotifWorker);

    const auto hresult = ::SetThreadDescription(gNotifWorker->native_handle(), L"WinCse::notifListener");
    APP_ASSERT(SUCCEEDED(hresult));

    return true;
}

void CSDeviceBase::notifListener() noexcept
{
    NEW_LOG_BLOCK();

    while (true)
    {
        const auto reason = ::WaitForMultipleObjects(gNumNotifEvents, gNotifEvents, FALSE, INFINITE);

        if (WAIT_OBJECT_0 <= reason && reason < (WAIT_OBJECT_0 + gNumNotifEvents))
        {
            // go next
        }
        else
        {
            const auto lerr = ::GetLastError();
            traceW(L"un-expected reason=%lu, lerr=%lu, break", reason, lerr);
            break;
        }

        if (gEndWorkerFlag)
        {
            traceW(L"catch end-thread request, break");
            break;
        }

        const auto eventId = reason - WAIT_OBJECT_0;
        const auto eventName = gEventNames[eventId];

        traceW(L"call onNotif eventId=%lu, eventName=%s", eventId, eventName);

        try
        {
            this->onNotif(START_CALLER eventId, eventName);
        }
        catch (const std::exception& ex)
        {
            traceA("what: %s", ex.what());
            break;
        }
        catch (...)
        {
            traceA("unknown error, continue");
        }
    }

    traceW(L"thread end");
}

void CSDeviceBase::deleteNotifListener(CALLER_ARG0)
{
    NEW_LOG_BLOCK();

    gEndWorkerFlag = true;

    for (int i=0; i<gNumNotifEvents; i++)
    {
        if (gNotifEvents[i])
        {
            const auto b = ::SetEvent(gNotifEvents[i]);
            APP_ASSERT(b);
        }
    }

    if (gNotifWorker)
    {
        traceW(L"join thread");
        gNotifWorker->join();

        delete gNotifWorker;
        gNotifWorker = nullptr;
    }

    for (int i=0; i<gNumNotifEvents; i++)
    {
        if (gNotifEvents[i])
        {
            ::CloseHandle(gNotifEvents[i]);
            gNotifEvents[i] = NULL;
        }
    }
}

// EOF