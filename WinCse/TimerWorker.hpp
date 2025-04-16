#pragma once

#include "WinCseLib.h"
#include "ScheduledWorker.hpp"

class TimerWorker : public ScheduledWorker
{
protected:
	int getThreadPriority() const noexcept override
	{
		return THREAD_PRIORITY_LOWEST;
	}

	DWORD getTimePeriodMillis() const noexcept override
	{
		return WCSE::TIMEMILLIS_1MINu;
	}

public:
	using ScheduledWorker::ScheduledWorker;
};

// EOF