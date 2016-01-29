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
    std::vector<SList<Task>> delete_lists;
    Task::PopDeleteList(delete_lists);
    for (auto &delete_list : delete_lists)
        for (auto it = delete_list.begin(); it != delete_list.end();)
        {
            Task* tk = &*it++;
            delete tk;
        }
    return 0;
}

void IoWait::Cancel(Task *tk, uint32_t id)
{

}

} //namespace co
