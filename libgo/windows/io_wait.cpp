#include "io_wait.h"
#include "error.h"
#include <algorithm>
#include "scheduler.h"

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
