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
#include "../profiler.h"
using namespace std;

static const int N = 10000000;

vector<co::Scheduler*> g_scheds;

inline void __WaitUntilNoTask(co::Scheduler & scheduler, int line, std::size_t val = 0) {
    int i = 0;
    while (scheduler.TaskCount() > val) {
        usleep(1000);
        if (++i == 9000) {
            printf("LINE: %d, TaskCount: %d\n", line, (int)g_Scheduler.TaskCount());
        }
    }
}

#define WaitUntilNoTaskS(scheduler) __WaitUntilNoTask(scheduler, __LINE__)
#define WaitUntilNoTask() do { \
        for (auto sched : g_scheds) { \
            WaitUntilNoTaskS(*sched); \
        } \
    } while (0);

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
    const int threads = TEST_MIN_THREAD;
    auto start = chrono::steady_clock::now();
    for (int i = 0; i < threads; ++i) {
        ++c;
        auto f = [&]{
            for (int i = 0; i < n; ++i) {
                mtx.lock();
                ++val;
                mtx.unlock();
            }
            --c;
        };
        go co_scheduler(g_scheds[i % g_scheds.size()]) f;
//        std::thread(f).detach();
    }

    while (c) {
        usleep(1000);
    }
    auto end = chrono::steady_clock::now();
    dump("BenchmarkMutex_" + std::to_string(val), val, start, end);
    if (val != n * threads)
        printf("ERROR, val=%ld\n", val);
}

int main(int argc, char** argv)
{
    int n = 1000000;
    if (argc > 1) {
        n = atoi(argv[1]);
    }
    printf("n = %d\n", n);

//    co_opt.debug = co::dbg_scheduler;
//    co_opt.debug_output = fopen("log", "w");

    for (int i = 0; i < TEST_MIN_THREAD; ++i) {
        auto sched = co::Scheduler::Create();
        sched->goStart(1);
        g_scheds.push_back(sched);
    }

    test_atomic();

    { 
//        GProfilerScope prof;
        test_mutex(n); 
    }

    return 0;

//    go []{ test_channel(0, 1000000); };
//    WaitUntilNoTask();
//
//    go []{ test_channel(1, 1000000); };
//    WaitUntilNoTask();

//    go []{ test_channel(63, 1000000); };
//    WaitUntilNoTask();
//
//    go []{ test_channel(64, 1000000); };
//    WaitUntilNoTask();

//    go []{ test_channel(9999, 10000000); };
//    WaitUntilNoTask();

//    go []{ test_channel(5, 10); };
//    go []{ test_channel(10000, 10000000); };
//    WaitUntilNoTask();
}
