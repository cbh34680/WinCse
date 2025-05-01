#include "NotifListener.hpp"

using namespace CSELIB;
using namespace CSEDRV;


std::unique_ptr<NotifListener> NotifListener::create(const std::list<ICSService*>& argServices) noexcept
{
	NEW_LOG_BLOCK();

	std::vector<std::pair<ICSService*, std::wstring>> notifs;
	std::set<std::wstring> already;

	for (auto* service: argServices)
	{
		const auto names{ service->getNotificationList() };

		for (const auto& name: names)
		{
			traceW(L"add name=%s", name.c_str());

			if (already.find(name) != already.cend())
			{
				traceW(L"fault: already exists name=%s", name.c_str());
				return nullptr;
			}

			notifs.emplace_back(service, name);

			already.insert(name);
		}
	}

	return std::unique_ptr<NotifListener>{ new NotifListener{ std::move(notifs), std::vector<HANDLE>(notifs.size(), NULL) } };
}

NTSTATUS NotifListener::start() noexcept
{
	NEW_LOG_BLOCK();

	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);

	// �Z�L�����e�B�L�q�q�̍쐬

	SECURITY_DESCRIPTOR sd{};
	if (!::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
	{
		traceW(L"fault: InitializeSecurityDescriptor");
		return FspNtStatusFromWin32(::GetLastError());
	}

	// ���ׂẴ��[�U�[�Ƀt���A�N�Z�X������
#pragma warning(suppress: 6248)
	if (!::SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE))
	{
		traceW(L"fault: SetSecurityDescriptorDacl");
		return FspNtStatusFromWin32(::GetLastError());
	}

	sa.lpSecurityDescriptor = &sd;

	for (int i=0; i<mEvents.size(); i++)
	{
		const auto* name = mNotifs[i].second.c_str();

		traceW(L"CreateEventW name=%s", name);

		mEvents[i] = ::CreateEventW(&sa, FALSE, FALSE, name);
		if (!mEvents[i])
		{
			traceW(L"fault: CreateEventW name=%s", name);
			return FspNtStatusFromWin32(::GetLastError());
		}
	}

	traceW(L"start Listen thread");

	mThread = std::make_unique<std::thread>(&NotifListener::listen, this);
	APP_ASSERT(mThread);

	const auto hresult = ::SetThreadDescription(mThread->native_handle(), L"NotifListener::listen");
	APP_ASSERT(SUCCEEDED(hresult));

	return STATUS_SUCCESS;
}

NTSTATUS NotifListener::stop() noexcept
{
	NEW_LOG_BLOCK();

	mEndThreadFlag = true;

	for (int i=0; i<mEvents.size(); i++)
	{
		if (mEvents[i])
		{
			const auto b = ::SetEvent(mEvents[i]);
			APP_ASSERT(b);
		}
	}

	if (mThread)
	{
		traceW(L"join thread");
		mThread->join();

		mThread.reset();
	}

	for (int i=0; i<mEvents.size(); i++)
	{
		if (mEvents[i])
		{
			::CloseHandle(mEvents[i]);
			mEvents[i] = NULL;
		}
	}

	return STATUS_SUCCESS;
}

void NotifListener::listen() noexcept
{
	NEW_LOG_BLOCK();

	traceW(L"start listen");

	const auto numEvents = (DWORD)mEvents.size();
	const auto* events = mEvents.data();

	while (true)
	{
		const auto reason = ::WaitForMultipleObjects(numEvents, events, FALSE, INFINITE);

		if (WAIT_OBJECT_0 <= reason && reason < (WAIT_OBJECT_0 + numEvents))
		{
			// go next
		}
		else
		{
			const auto lerr = ::GetLastError();
			traceW(L"un-expected reason=%lu, lerr=%lu, break", reason, lerr);
			break;
		}

		if (mEndThreadFlag)
		{
			traceW(L"catch end-thread request, break");
			break;
		}

		try
		{
			const auto eventId = reason - WAIT_OBJECT_0;
			const auto& notif = mNotifs[eventId];

			auto* service = notif.first;
			const auto* eventName = notif.second.c_str();

			const auto klassName{ getDerivedClassNamesW(service) };

			traceW(L"%s::onNotif eventId=%lu, eventName=%s", klassName.c_str(), eventId, eventName);

			if (!service->onNotif(eventName))
			{
				traceW(L"fault: %s::onNotif eventId=%lu, eventName=%s", klassName.c_str(), eventId, eventName);
			}
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

	traceW(L"exit listen");
}


// EOF