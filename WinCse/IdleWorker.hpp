#pragma once

#include "ScheduledWorker.hpp"

class IdleWorker : public ScheduledWorker
{
protected:
	int getThreadPriority() const noexcept override
	{
#ifdef _DEBUG
		return THREAD_PRIORITY_LOWEST;
#else
		return THREAD_PRIORITY_IDLE;
#endif
	}

	DWORD getTimePeriodMillis() const noexcept override
	{
		return WinCseLib::TIMEMILLIS_1MINu;
	}

public:
	using ScheduledWorker::ScheduledWorker;
};

// EOF