#include <chrono>
#include <iostream>
#include <atomic>
#include <string>
#include "../../libgo/libgo.h"
#if TEST_MIN_THREAD
#else
#define TEST_MIN_THREAD 8
#define TEST_MAX_THREAD 8
#endif
#include "../gtest_unit/gtest_exit.h"
using namespace std;

static const int N = 10000000;

template <typename T>
void dump(string name, int n, T start, T end)
{
    cout << name << "    " << n << "      " << 
        chrono::duration_cast<chrono::nanoseconds>(end - start).count() / n << " ns/op" << endl;
//    cout << "ok. cost " << chrono::duration_cast<chrono::milliseconds>(end - start).count() << " ms" << endl;
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
            for (int i = 0; i < N / coro; ++i)
                co_yield;
            if (++*done == coro) {
                auto end = chrono::steady_clock::now();
                dump("BenchmarkSwitch_" + std::to_string(coro), N, start, end);
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
