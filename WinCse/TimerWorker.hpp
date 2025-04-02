#pragma once

class TimerWorker : public ScheduledWorker
{
protected:
	int getThreadPriority() const noexcept override
	{
		return THREAD_PRIORITY_BELOW_NORMAL;
	}

	DWORD getTimePeriodMillis() const noexcept override
	{
		return WinCseLib::TIMEMILLIS_1MINu;
	}

public:
	using ScheduledWorker::ScheduledWorker;
};

// EOF