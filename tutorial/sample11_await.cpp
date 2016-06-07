/************************************************
 * libgo sample10
************************************************
 * 除了网络IO之外, 有时也难免需要执行一些阻塞式的系统调用
 * 比如: gethostbyname_r, 文件IO等等
 *
 * 如果直接调用, 这些会真正阻塞调度线程,
 * 因此需要使用co_await将操作投递值线程池中完成
************************************************/
#include <netdb.h>
#include <chrono>
#include <iostream>
#include <boost/thread.hpp>
#include "coroutine.h"
#include "win_exit.h"
using namespace std;
using namespace std::chrono;

// 一个阻塞操作
hostent* block1(const char *name)
{
    printf("do block1\n");
    return gethostbyname(name);
}

void block2()
{
    printf("do block2\n");
    gethostbyname("www.google.com");
}

void foo()
{
    printf("start co_await\n");
    /** co_await是一个宏, 需要一个参数: 返回类型
     *  co_await(type)后面要写一个可调用对象 (lambda, 函数指针, function对象等等, 规则和go关键字相同)
     *  在co_await内部会将操作任务到线程池中, 并使用channel等待任务完成。
     */
    hostent* ht = co_await(hostent*) []{ return block1("www.baidu.com"); };
    (void)ht;

    /** co_await也可以用于无返回值的调用
     *
     */
    co_await(void) block2;

    // 不要投递会无限等待的任务到线程池中, 会导致线程池的一个调度线程被阻塞,
    // 一旦线程池的调度线程全部被阻塞, 线程池就无法工作了.
    printf("done co_await\n");
}

int main(int argc, char** argv)
{
    // 如果需要使用线程池功能, 需要用户自行创建线程去Run线程池.
    boost::thread_group tg;
    for (int i = 0; i < 4; ++i) {
        tg.create_thread([]{co_sched.GetThreadPool().RunLoop();});
    }

    go foo;
    go foo;
    go foo;
    go foo;
    co_sched.RunUntilNoTask();

    return 0;
}

