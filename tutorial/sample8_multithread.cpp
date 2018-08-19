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

// 大计算量的函数
int c = 0;
std::atomic<int> done{0};
void foo()
{
    int v = 1;
    for (int i = 1; i < 20000000; ++i) {
        v *= i;
    }
    c += v;

    if (++done == 200)
        co_sched.Stop();
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
    co_sched.Start(8);

    end = system_clock::now();
    cout << "go with coroutine, cost ";
    cout << duration_cast<milliseconds>(end - start).count() << "ms" << endl;
	cout << "result zero:" << c * 0 << endl;
    return 0;
}

