#include "io_wait.h"
#include "error.h"
#include <algorithm>
#include "scheduler.h"

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
    std::vector<SList<Task>> delete_lists;
    Task::PopDeleteList(delete_lists);
    for (auto &delete_list : delete_lists)
        for (auto it = delete_list.begin(); it != delete_list.end();)
        {
            Task* tk = &*it++;
            delete tk;
        }
    if (enable_block)
        usleep(epollwait_ms_ * 1000);
    return 0;
}

void IoWait::Cancel(Task *tk, uint32_t id)
{

}

} //namespace co
