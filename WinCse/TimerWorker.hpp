#pragma once

#include "ScheduledWorker.hpp"

namespace CSEDRV
{

class TimerWorker final : public ScheduledWorker
{
protected:
	int getThreadPriority() const override
	{
		return THREAD_PRIORITY_LOWEST;
	}

	DWORD getTimePeriodMillis() const override
	{
		return CSELIB::TIMEMILLIS_1MINu;
	}

public:
	using ScheduledWorker::ScheduledWorker;
};

}	// namespace CSELIB

// EOF