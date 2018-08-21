/************************************************
 * libgo sample1
*************************************************/
#include "coroutine.h"
#include "win_exit.h"
#include <stdio.h>
#include <thread>

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
    //----------------------------------
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
    go co_stack(10 * 1024 * 1024) []{
        printf("large stack\n");
    };

    // 协程创建以后不会立即执行，而是暂存至可执行列表中，等待调度器调度。
    // co_sched是默认的协程调度器，用户也可以使用自创建的协程调度器。 
    // 当仅使用一个线程进行协程调度时, 协程地执行会严格地遵循其创建顺序.

    // 仅使用主线程调度协程.
    // co_sched.Start();

    // 以下代码可以使用等同于cpu核心数的线程调度协程.(包括主线程)
    // co_sched.Start(0);

    // 以下代码允许调度器自由扩展线程数，上限为1024.
    // 当有线程被协程阻塞时, 调度器会启动一个新的线程, 以此保障
    // 可用线程数总是等于Start的第一个参数(0表示cpu核心数).
    // co_sched.Start(0, 1024);

    // 如果不想让调度器卡住主线程, 可以使用以下方式:
    std::thread t([]{ co_sched.Start(); });
    t.detach();
    co_sleep(100);
    //----------------------------------

    //----------------------------------
    // 除了上述的使用默认的调度器外, 还可以自行创建额外的调度器,
    // 协程只会在所属的调度器中被调度, 创建额外的调度器可以实现业务间的隔离.

    // 创建一个调度器
    co::Scheduler* sched = co::Scheduler::Create();

    // 启动4个线程执行新创建的调度器
    std::thread t2([sched]{ sched->Start(4); });
    t2.detach();

    // 在新创建的调度器上创建一个协程
    go co_scheduler(sched) []{
        printf("run in my scheduler.\n");
    };

    co_sleep(100);
    return 0;
}

