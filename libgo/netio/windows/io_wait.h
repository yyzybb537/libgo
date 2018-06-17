#pragma once
#include <unistd.h>
#include <vector>
#include <list>
#include <set>
#include <libgo/task.h>

namespace co
{
    class IoWait
    {
    public:
        void SchedulerSwitch(Task* tk);

        int WaitLoop(int);
    };

} //namespace co
