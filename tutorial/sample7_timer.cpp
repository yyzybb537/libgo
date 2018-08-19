/************************************************
 * libgo sample7
************************************************
 * libgo库原生提供了一个线程安全的定时器
 *
 * 还提供了休眠当前协程的方法co_sleep，类似于系统调用sleep, 不过时间单
 * 位是毫秒.
 * 同时HOOK了系统调用sleep、usleep、nanosleep, 在协程中使用这几个系统
 * 调用, 会在等待期间让出cpu控制权, 执行其他协程, 不会阻塞调度线程.
************************************************/
#include "coroutine.h"
#include "win_exit.h"

int main()
{
    // 创建一个定时器
    // 第一个参数: 精度
    // 第二个参数: 绑定到一个调度器(Scheduler)
    // 两个参数都有默认值, 可以简便地创建一个定时器: co_timer timer; 
    co_timer timer(std::chrono::milliseconds(1), &co_sched);

    // 使用timer.ExpireAt接口设置一个定时任务
    // 第一个参数可以是std::chrono中的时间长度，也可以是时间点。
    // 第二个参数是定时器回调函数
    // 返回一个co_timer_id类型的ID, 通过这个ID可以撤销还未执行的定时函数
    co_timer_id id1 = timer.ExpireAt(std::chrono::seconds(1), []{
            printf("Timer Callback.\n");
            });

    // co_timer_id::StopTimer接口可以撤销还未开始执行的定时函数
    // 它返回bool类型的结果，如果撤销成功，返回true；
    //     如果未来得及撤销，返回false, 此时不保证回调函数已执行完毕。
    bool cancelled = id1.StopTimer();
    printf("cancelled:%s\n", cancelled ? "true" : "false");

    timer.ExpireAt(std::chrono::seconds(2), [&]{
            printf("Timer Callback.\n");
            co_sched.Stop();
            });

    for (int i = 0; i < 100; ++i)
        go []{
            // 休眠当前协程 1000 milliseconds.
            // 不会阻塞线程, 因此100个并发的休眠, 总共只需要1秒.
            co_sleep(1000);
        };

#if !defined(_WIN32)
    // 系统调用提供的sleep usleep nanosleep都使用HOOK技术,
    // 使其在协程中运行时, 能达到和co_sleep相同的效果.
    go []{
        // 休眠当前协程 1 second
        sleep(1);
    };

    go []{
        // 休眠当前协程 100 milliseconds
        usleep(100 * 1000);
    };
#endif

    co_sched.Start();
    return 0;
}

