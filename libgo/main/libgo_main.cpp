#include <libgo/coroutine.h>

extern "C" int __coroutine_main_function(int argc, char **argv);

#if __GNUC__
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
