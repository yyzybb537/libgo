#include "io_wait.h"
#include "error.h"

namespace co {

IoWait::IoWait() 
{

}

void IoWait::CoSwitch(std::vector<FdStruct> && fdsts, int timeout_ms)
{

}

void IoWait::SchedulerSwitch(Task* tk)
{

}

int IoWait::WaitLoop()
{
	Task::DeleteList delete_list;
	Task::SwapDeleteList(delete_list);
	for (auto tk : delete_list)
		delete tk;
    return 0;
}

void IoWait::Cancel(Task *tk, uint32_t id)
{

}

} //namespace co
