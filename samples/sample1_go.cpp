/************************************************
 * libgo是一个使用C++11编写的调度式stackful协程库,
 * 同时也是一个强大的并行编程库，
 * 可以运行在Linux和Win7-64bit上.
 * 是专为linux服务端程序开发设计的底层框架。
 *
 * 使用libgo编写并行程序，即可以像go、erlang这些
 * 并发语言一样开发迅速且逻辑简洁，又有C++原生的性能优势
 * 鱼和熊掌从此可以兼得。
 *
 * libgo有以下特点：
 *   1.提供与golang同样强大的协程(CSP模型)
 *     基于libgo编写代码，可以以同步的方式
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
 * libgo sample1
*************************************************/
#include "coroutine.h"
#include "win_exit.h"
#include <stdio.h>
#include <boost/thread.hpp>

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

    // 也可以使用go_stack创建指定栈大小的协程
    //   创建拥有10MB大栈的协程
    go_stack(10 * 1024 * 1024) []{
        printf("large stack\n");
    };

    // 协程创建以后不会立即执行，而是暂存至可执行列表中，等待调度器调度。
    // co_sched是全局唯一的协程调度器，有以下接口可以调度协程：
    //   1.Run 执行单次调度, 返回本次执行的协程数量
    //   2.RunLoop 无限循环执行Run, 不会返回
    //   3.RunUntilNoTask 循环执行Run, 直至协程数量为零.
    //
    // 当仅使用一个线程进行协程调度时, 协程地执行会严格地遵循其创建顺序.
    co_sched.RunUntilNoTask();

    // 多线程模式下, libgo还支持指定协程初始运行于哪个线程
    // 使用go_dispatch关键字来创建协程, 可以分派协程执行的线程.
    // 支持多种分派模式
    // 1.指定线程索引分派 (线程索引从0起, 按照调用Run的顺序决定线程索引)
    go_dispatch(2) []{
        printf("dispatch to thread[2] run\n");
    };
    // 2.随机 (调用过Run的线程才会参与随机指派)
    go_dispatch(egod_random) []{
        printf("random run\n");
    };
    // 3.robin算法 (调用过Run的线程, 或强制执行线程索引分派过协程的线程, 才会参与随机指派)
    go_dispatch(egod_robin) []{
        printf("robin run\n");
    };
    // 4.尽量置于当前线程 (只有当当前线程已经调用过Run后才生效)
    go_dispatch(egod_local_thread) []{
        printf("local thread run\n");
    };

    // 启动额外两个线程和主线程一起调度
    boost::thread_group tg;
    for (int i = 0; i < 2; ++i)
        tg.create_thread([]{ co_sched.RunUntilNoTask(); });
    co_sched.RunUntilNoTask();
    tg.join_all();

    // 默认配置下, 多线程调度时会采用worksteal算法做负载均衡, dispatch指定的协程也可能被其他
    // 线程偷走, 如果不希望被偷走, 可以关闭worksteal算法.
    co_sched.GetOptions().enable_work_steal = false;    // 关闭worksteal负载均衡算法

    return 0;
}

