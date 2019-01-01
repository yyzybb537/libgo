/************************************************
 * libgo sample8
************************************************
 * libgo既是协程库, 同时也是一个高效的并行编程库.
************************************************/
#include <chrono>
#include <iostream>
#include <atomic>
#include "coroutine.h"
#include "win_exit.h"
using namespace std;
using namespace std::chrono;

const int nWork = 100;

// 大计算量的函数
int c = 0;
std::atomic<int> done{0};
void foo()
{
    int v = (int)rand();
    for (int i = 1; i < 20000000; ++i) {
        v *= i;
    }
    c += v;

    if (++done == nWork * 2)
        co_sched.Stop();
}

int main()
{
    // 编写cpu密集型程序时, 可以延长协程执行的超时判断阈值, 避免频繁的worksteal产生
    co_opt.cycle_timeout_us = 1000 * 1000;

    // 普通的for循环做法
    auto start = system_clock::now();
    for (int i = 0; i < nWork; ++i)
        foo();
    auto end = system_clock::now();
    cout << "for-loop, cost ";
    cout << duration_cast<milliseconds>(end - start).count() << "ms" << endl;

    // 使用libgo做并行计算
    start = system_clock::now();
    for (int i = 0; i < nWork; ++i)
        go foo;

    // 创建8个线程去并行执行所有协程 (由worksteal算法自动做负载均衡)
    co_sched.Start(8);

    end = system_clock::now();
    cout << "go with coroutine, cost ";
    cout << duration_cast<milliseconds>(end - start).count() << "ms" << endl;
	cout << "result zero:" << c * 0 << endl;
    return 0;
}

