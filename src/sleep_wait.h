/************************************************
 * Wait for sleep, nanosleep or poll(NULL, 0, timeout)
*************************************************/
#pragma once
#include <vector>
#include <list>
#include "task.h"

namespace co
{

class SleepWait
{
public:
    // 在协程中调用的switch, 暂存状态并yield
    void CoSwitch(int timeout_ms);

    // 在调度器中调用的switch
    void SchedulerSwitch(Task* tk);

    uint32_t WaitLoop();

private:
    void Wakeup(Task *tk);

    CoTimerMgr timer_mgr_;

    typedef TSQueue<Task> TaskList;
    TaskList wait_tasks_;
};


} //namespace co
