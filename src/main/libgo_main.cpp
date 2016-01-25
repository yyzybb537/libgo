#include "coroutine.h"

extern "C" int __coroutine_main_function(int argc, char **argv);

#ifndef _WIN32
__attribute__((weak)) 
#endif
int main(int argc, char **argv)
{
    co_chan<int> c(1);

    go [=]{
        c << __coroutine_main_function(argc, argv);
    };

    co_sched.RunUntilNoTask();
    int ret = 1;
    c.TryPop(ret);
    return ret;
}
