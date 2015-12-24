#pragma once
#include <unistd.h>
#include <vector>
#include <list>
#include <set>
#include "task.h"

namespace co
{

    class IoWait
    {
    public:
        IoWait();

        // 在协程中调用的switch, 暂存状态并yield
        void CoSwitch(std::vector<FdStruct> && fdsts, int timeout_ms);

        // 在调度器中调用的switch, 如果成功则进入等待队列，如果失败则重新加回runnable队列
        void SchedulerSwitch(Task* tk);

        int WaitLoop();

    private:
        void Cancel(Task *tk, uint32_t id);
    };


} //namespace co
