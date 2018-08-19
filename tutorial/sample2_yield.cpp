/************************************************
 * libgo sample2
*************************************************/
#include "coroutine.h"
#include "win_exit.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    // 在协程中使用co_yield关键字, 可以主动让出调度器执行权限,
    // 让调度器有机会去执行其他协程,
    // 并将当前协程移动到可执行协程列表的尾部。
    // 类似于操作系统提供的sleep(0)的功能。
    //
    // 在下面的例子中, 由于co_yield的存在, 两个协程会交错执行,
    // 输出结果为:
    //   1
    //   3
    //   2
    //   4
    //
    // 注意：1.在协程外使用co_yield不会有任何效果，也不会出错。
    //       2.不要忘记co_yield语句后面的分号";"
    go []{
        printf("1\n");
        co_yield;
        printf("2\n");
    };

    go []{
        printf("3\n");
        co_yield;
        printf("4\n");

        // 使用主线程调度时, 可以使用Stop来安全地退出main函数
        // 注意：Stop是一个不可逆的操作, 除非是进程退出的时候, 否则你不应该使用它.
        co_sched.Stop();
    };

    co_sched.Start();
    return 0;
}

