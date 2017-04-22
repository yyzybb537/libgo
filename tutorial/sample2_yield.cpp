/************************************************
 * libgo sample2
*************************************************/
#include <libgo/coroutine.h>
#include "win_exit.h"
#include <stdio.h>

// 也可以使用co_main宏定义main函数, 
// 此时, main函数也是执行在协程中, 不要再执行调度器!
// 链接时需要增加参数：-llibgo_main
co_main(int argc, char **argv)
{
    // 在协程中使用co_yield关键字, 可以主动让出调度器执行权限,
    // 让调度器有机会去执行其他协程,
    // 并将当前协程加到可执行协程列表的尾部。
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
    //       2.不要忘记co_yield语句后面的分号";", 如果忘记，也
    //         没有太大关系，编译器一定会不太友好地提醒你。
    //
    go []{
        printf("1\n");
        co_yield;
        printf("2\n");
    };

    go []{
        printf("3\n");
        co_yield;
        printf("4\n");
    };

    return 0;
}

