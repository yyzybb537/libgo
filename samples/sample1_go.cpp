/************************************************
 * coroutine是一个使用C++11编写的调度式stackful协程库,
 * 同时也是一个强大的并行编程库，
 * 可以运行在Linux和Win7-64bit上.
 * 是专为linux服务端程序开发设计的底层框架。
 *
 * 使用coroutine编写并行程序，即可以像go、erlang这些
 * 并发语言一样开发迅速且逻辑简洁，又有C++原生的性能优势
 * 鱼和熊掌从此可以兼得。
 *
 * coroutine有以下特点：
 *   1.提供不输与golang的强大的协程
 *     基于corontine编写代码，可以以同步的方式
 *     编写简单的代码，同时获得异步的性能，
 *   2.允许用户自主控制协程调度点
 *   3.支持多线程调度协程，极易编写并行代码，
 *     高效的并行调度算法，可以有效利用多个CPU核心
 *   4.采用hook-socket函数族的方式，可以让链接
 *     进程序的同步的第三方库变为异步调用，大大
 *     提升其性能。
 *     再也不用担心某些DB官方不提供异步driver了，
 *     比如hiredis、mysqlclient这种客户端驱动
 *     可以直接使用，并且可以得到不输于异步
 *     driver的性能。
 *   5.动态链接和静态链接全都支持
 *     便于使用C++11的用户使用静态链接生成可执行
 *     文件并部署至低版本的linux系统上。
 *   6.提供协程锁(co_mutex), 定时器, channel等特性,
 *     帮助用户更加容易地编写程序. 
 *
 * 如果你发现了任何bug、有好的建议、或使用上有不明之处，
 * 请联系作者:
 *     email:  289633152@qq.com
*************************************************
 * 如果你在使用linux系统查看源码, 并且Gcc编译器支持C++11,
 * 那么可以在corouotine目录执行过make && make install后
 * 在当前目录使用make命令生成所有samples的可执行文件，
 * 以运行他们，查看运行结果。
 *
 * 每个sample会对应生成两个可执行文件，例如：
 * sample1会生成 sample1.t 和 s_sample1.t.
 * 以s_开头的是静态链接的产物，另一个是动态链接的产物，
 * 两个sample的执行结果会是相同。
*************************************************
 * coroutine sample1
*************************************************/
#include "coroutine.h"
#include <stdio.h>

void foo()
{
    printf("function pointer\n");
}

struct A {
    void fA() { printf("std::bind\n"); }
    void fB() { printf("std::function\n"); }
};

int main()
{
    // 使用关键字go创建协程, go后面可以使用:
    //     1.void(*)()函数指针, 比如:foo.
    //     2.也可以使用无参数的lambda, std::bind对象, function对象, 
    //     3.以及一切可以无参调用的仿函数对象
    //   注意不要忘记句尾的分号";".
    go foo;

    go []{
        printf("lambda\n");
    };

    go std::bind(&A::fA, A());

    std::function<void()> fn(std::bind(&A::fB, A()));
    go fn;

    // 协程创建以后不会立即执行，而是暂存至可执行列表中，等待调度器调度。
    // co_sched是全局唯一的协程调度器，有以下接口可以调度协程：
    //   1.Run 执行单次调度, 返回本次执行的协程数量
    //   2.RunLoop 无限循环执行Run, 不会返回
    //   3.RunUntilNoTask 循环执行Run, 直至协程数量为零.
    //
    // 当仅使用一些线程进行协程调度时, 协程地执行会严格地遵循其创建顺序.
    co_sched.RunUntilNoTask();
    return 0;
}

