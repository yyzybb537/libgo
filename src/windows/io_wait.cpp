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
    SList<Task> delete_list;
    Task::PopDeleteList(delete_list);
    for (auto it = delete_list.begin(); it != delete_list.end();)
    {
        Task* tk = &*it++;
        DebugPrint(dbg_task, "task(%s) delete.", tk->DebugInfo());
        delete tk;
    }
    return 0;
}

void IoWait::Cancel(Task *tk, uint32_t id)
{

}

} //namespace co
