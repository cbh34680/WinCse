#pragma once

#include "CSDriverCommon.h"

namespace CSEDRV
{

class NotifListener final
{
private:
	const std::vector<std::pair<CSELIB::ICSService*, std::wstring>>	mNotifs;
	std::vector<HANDLE>												mEvents;
	std::unique_ptr<std::thread>									mThread;
	std::atomic<bool>												mEndThreadFlag;

	NotifListener(
		std::vector<std::pair<CSELIB::ICSService*, std::wstring>>&& argNotifs,
		std::vector<HANDLE>&& argEvents) noexcept
		:
		mNotifs(std::move(argNotifs)),
		mEvents(std::move(argEvents))
	{
	}

public:
	static std::unique_ptr<NotifListener> create(const std::list<CSELIB::ICSService*>& argServices) noexcept;

	~NotifListener()
	{
		this->stop();
	}

	NTSTATUS start() noexcept;
	void listen() noexcept;
	NTSTATUS stop() noexcept;
};

}	// CSEDRV

// EOF