#include "gtest/gtest.h"
#include <boost/timer.hpp>
#include <vector>
#include <list>
#include <atomic>
#include <thread>
#include <mutex>
#include "coroutine.h"
#include "gtest_exit.h"
using namespace std::chrono;
using namespace co;

void test()
{
    std::mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);
    std::unique_lock<std::mutex> lock2(mtx, std::defer_lock);
    EXPECT_FALSE(lock2.try_lock());

    std::atomic_int v{0};
    int check = 0;
    for (int i = 1; i <= 10000; ++i)
    {
        check += i;
        go [&, i]{
            v += co_await(int) [i]{
                usleep(100);
                return i;
            };
        };
    }

    co_sched.RunUntilNoTask();
    EXPECT_EQ(check, (int)v);
}

void loop()
{
    go []{
        co_await(void) []{};
    };
    co_sched.Run();

    std::vector<std::thread> vthreads;
    for (int i = 0; i < 10; ++i)
        vthreads.emplace_back([]{
                co_sched.GetThreadPool().RunLoop();
                });

    test();
    for (auto &t : vthreads)
        t.detach();
}

TEST(testThreadPool, testThreadPool)
{
    loop();
}
