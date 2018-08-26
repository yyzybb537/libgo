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
    co_chan<bool> c(capa);
    auto start = chrono::steady_clock::now();
    go [=]{
        for (int i = 0; i < n; ++i) {
            c << true;
        }
    };
    for (int i = 0; i < n; ++i)
        c >> nullptr;
    auto end = chrono::steady_clock::now();
    dump("BenchmarkChannel_" + std::to_string(capa), n, start, end);
}

int main()
{
    test_atomic();

    go []{ test_switch(1); };
    WaitUntilNoTask();

    go []{ test_switch(1000); };
    WaitUntilNoTask();

    go []{ test_channel(0, N); };
    WaitUntilNoTask();

    go []{ test_channel(1, N); };
    WaitUntilNoTask();

    go []{ test_channel(10000, 10000); };
    WaitUntilNoTask();
}
