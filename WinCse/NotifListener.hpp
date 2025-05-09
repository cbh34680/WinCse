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
		std::vector<HANDLE>&& argEvents)
		:
		mNotifs(std::move(argNotifs)),
		mEvents(std::move(argEvents))
	{
	}

public:
	static std::unique_ptr<NotifListener> create(const std::list<CSELIB::ICSService*>& argServices);

	~NotifListener()
	{
		this->stop();
	}

	NTSTATUS start();
	void listen();
	NTSTATUS stop();
};

}	// CSEDRV

// EOF