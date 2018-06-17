/************************************************
 * Wait for sleep, nanosleep or poll(NULL, 0, timeout)
*************************************************/
#pragma once
#include <libgo/config.h>
#include <libgo/task.h>
#include <libgo/debugger.h>

namespace co
{

class SleepWait
{
public:
    // 在协程中调用的switch, 暂存状态并yield
    void CoSwitch(int timeout_ms);

    // 在调度器中调用的switch
    void SchedulerSwitch(Task* tk);

    // @next_ms: 距离下一个timer触发的毫秒数
    uint32_t WaitLoop(long long &next_ms);

private:
    void Wakeup(Task *tk);

    CoTimerMgr timer_mgr_;

    typedef TSQueue<Task> TaskList;
    TaskList wait_tasks_;

    friend class CoDebugger;
};


} //namespace co
