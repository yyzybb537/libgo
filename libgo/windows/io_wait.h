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
        void SchedulerSwitch(Task* tk);

        int WaitLoop(bool enable_block);

        void DelayEventWaitTime();
        void ResetEventWaitTime();

    private:
        int epollwait_ms_;
    };

} //namespace co
