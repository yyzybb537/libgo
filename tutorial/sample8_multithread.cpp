/************************************************
 * libgo sample8
************************************************
 * libgo作为并发编程库使用。
************************************************/
#include <chrono>
#include <iostream>
#include <boost/thread.hpp>
#include "coroutine.h"
#include "win_exit.h"
using namespace std;
using namespace std::chrono;

// 大计算量的函数
int c = 0;
void foo()
{
    int v = 1;
    for (int i = 1; i < 20000000; ++i)
        v *= i;
	c += v;
}

int main()
{
    // 普通的for循环做法
    auto start = system_clock::now();
    for (int i = 0; i < 100; ++i)
        foo();
    auto end = system_clock::now();
    cout << "for-loop, cost ";
    cout << duration_cast<milliseconds>(end - start).count() << "ms" << endl;

    // 使用libgo做并行计算
    start = system_clock::now();
    for (int i = 0; i < 100; ++i)
        go foo;

    // 创建8个线程去并行执行所有协程 (由worksteal算法自动做负载均衡)
    boost::thread_group tg;
    for (int i = 0; i < 8; ++i)
        tg.create_thread([] {
                co_sched.RunUntilNoTask();
                });
    tg.join_all();

    end = system_clock::now();
    cout << "go with coroutine, cost ";
    cout << duration_cast<milliseconds>(end - start).count() << "ms" << endl;
	cout << "result zero:" << c * 0 << endl;
    return 0;
}

