#include <chrono>
#include <iostream>
#include <atomic>
#include <string>
#include <stdio.h>
#include "../../libgo/libgo.h"
#if !defined(TEST_MIN_THREAD)
#define TEST_MIN_THREAD 1
#define TEST_MAX_THREAD 1
#endif
#include "../gtest_unit/gtest_exit.h"
//#include "../profiler.h"
using namespace std;

static const int N = 10000000;

template <typename T>
void dump(string name, int n, T start, T end)
{
    long nanos = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
    long nPerSecond = 100000 * (long)n / nanos;
    printf("%s    %d    %ld ns/op    %ld w/s\n", name.c_str(), n, nanos/n, nPerSecond);
}

void test_atomic()
{
    auto start = chrono::steady_clock::now();
    std::atomic_long val{0};
    for (int i = 0; i < N; ++i) {
        val += i;
    }
    auto end = chrono::steady_clock::now();
    dump("std::atomic.add", N, start, end);
}

void test_switch(int coro)
{
    auto start = chrono::steady_clock::now();
    std::atomic<int> *done = new std::atomic<int>{0};
    for (int i = 0; i < coro; ++i)
        go co_stack(8192) [=]{
            for (int i = 0; i < N * 10 / coro; ++i)
                co_yield;
            if (++*done == coro) {
                auto end = chrono::steady_clock::now();
                dump("BenchmarkSwitch_" + std::to_string(coro), N * 10, start, end);
                delete done;
            }
        };
}

void test_channel(int capa, int n)
{
    char buf[1024] = {};
    co_chan<bool> ch(capa);
    std::atomic_int c {0};
//    GProfilerScope prof;
    auto start = chrono::steady_clock::now();
    const int loop = std::max(TEST_MIN_THREAD / 2, 1);
    for (int i = 0; i < loop; ++i) {
        c += 2;
        go [&]{
            for (int i = 0; i < n; ++i) {
                ch << true;
            }
            --c;
        };

        go [&]{
            for (int i = 0; i < n; ++i) {
                ch >> nullptr;
            }
            --c;
        };
    }

    while (c) {
        usleep(1000);
    }
    auto end = chrono::steady_clock::now();
    dump("BenchmarkChannel_" + std::to_string(capa), n * loop, start, end);
}

void test_mutex(int n)
{
//    typedef co::LFLock mutex_t;
//    typedef std::mutex mutex_t;
    typedef co_mutex mutex_t;
    mutex_t mtx;
    std::atomic_int c {0};
    long val = 0;
    auto start = chrono::steady_clock::now();
    for (int i = 0; i < TEST_MIN_THREAD; ++i) {
        ++c;
        go [&]{
            for (int i = 0; i < n; ++i) {
                std::unique_lock<mutex_t> lock(mtx);
                free(malloc(400));
                ++val;
            }
            --c;
        };
    }

    while (c) {
        usleep(1000);
    }
    auto end = chrono::steady_clock::now();
    dump("BenchmarkMutex_" + std::to_string(val), val, start, end);
    if (val != n * TEST_MIN_THREAD)
        printf("ERROR, val=%ld\n", val);
}

int main()
{
//    co_opt.debug = co::dbg_channel;
//    co_opt.debug_output = fopen("log", "w");

    test_atomic();

    go []{ test_switch(1); };
    WaitUntilNoTask();

    go []{ test_switch(1000); };
    WaitUntilNoTask();

//    go []{ test_mutex(1000000); };
//    WaitUntilNoTask();

    go []{ test_channel(0, 1000000); };
    WaitUntilNoTask();

    go []{ test_channel(1, 1000000); };
    WaitUntilNoTask();

//    go []{ test_channel(63, 1000000); };
//    WaitUntilNoTask();
//
//    go []{ test_channel(64, 1000000); };
//    WaitUntilNoTask();

//    go []{ test_channel(9999, 10000000); };
//    WaitUntilNoTask();

//    go []{ test_channel(5, 10); };
    go []{ test_channel(10000, 10000000); };
    WaitUntilNoTask();
}
