#include "io_wait.h"
#include "error.h"
#include <algorithm>
#include "scheduler.h"

namespace co {

void IoWait::SchedulerSwitch(Task* tk)
{
	(void)tk;
}

void IoWait::DelayEventWaitTime()
{
    ++epollwait_ms_;
    epollwait_ms_ = std::min<int>(epollwait_ms_, g_Scheduler.GetOptions().max_sleep_ms);
}

void IoWait::ResetEventWaitTime()
{
    epollwait_ms_ = 0;
}

int IoWait::WaitLoop(bool enable_block)
{
	return -1;
}

} //namespace co
