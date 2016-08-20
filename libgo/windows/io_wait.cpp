#include "io_wait.h"
#include <libgo/error.h>
#include <algorithm>
#include <libgo/scheduler.h>

namespace co {

void IoWait::SchedulerSwitch(Task* tk)
{
	(void)tk;
}

int IoWait::WaitLoop(int)
{
	return -1;
}

} //namespace co
