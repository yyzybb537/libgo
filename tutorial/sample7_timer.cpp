/************************************************
 * libgo sample7
************************************************
 * libgo库原生提供了一个线程安全的定时器
 *  协程内外均可使用. 加入定时器的回调函数会在调度器Run时被触发.
 *
 * 还提供了休眠当前协程的方法co_sleep，类似于系统调用sleep, 不过时间单
 * 位是毫秒.
 * 同时HOOK了系统调用sleep、usleep、nanosleep, 在协程中使用这几个系统
 * 调用, 会自动变为co_sleep, 协程外使用效果不变.
************************************************/
#include <chrono>
#include "coroutine.h"
#include "win_exit.h"

int main()
{
    bool is_exit = false;

    // co_timer_add接受两个参数
    // 第一个参数可以是std::chrono中的时间长度，也可以是时间点。
    // 第二个参数是定时器回调函数
    // 返回一个co_timer_id类型的ID, 通过这个ID可以撤销还未执行的定时函数
    co_timer_id id1 = co_timer_add(std::chrono::seconds(1), []{
            printf("Timer Callback.\n");
            });

    // co_timer_cancel接口可以撤销还未执行的定时函数
    // 它只接受一个参数，就是co_timer_add返回的ID。
    // 它返回bool类型的结果，如果撤销成功，返回true；
    //     如果未来得及撤销，返回false, 此时不保证回调函数已执行完毕。
    //     如果需要保证回调函数不再撤销失败以后被执行, 需要使用co_timer_block_cancel接口
    bool cancelled = co_timer_cancel(id1);
    printf("cancelled:%s\n", cancelled ? "true" : "false");

    co_timer_add(std::chrono::seconds(2), [&]{
            printf("Timer Callback.\n");
            is_exit = true;
            });

    for (int i = 0; i < 100; ++i)
        go []{
            // 休眠当前协程 1000 milliseconds.
            // 不会阻塞线程, 因此100个并发的休眠, 总共只需要1秒.
            co_sleep(1000);
        };

#if !defined(_WIN32)
    go []{
        // 休眠当前协程 1 second
        sleep(1);
    };

    go []{
        // 休眠当前协程 100 milliseconds
        usleep(100 * 1000);
    };
#endif

    while (!is_exit)
        co_sched.Run();
    return 0;
}

