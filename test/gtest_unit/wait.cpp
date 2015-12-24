#include "gtest/gtest.h"
#include <boost/thread.hpp>
#include <boost/timer.hpp>
#include <vector>
#include <list>
#include <atomic>
#include "coroutine.h"
#include <chrono>
using namespace std::chrono;
using namespace co;

#define EXPECT_YIELD(n) EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), n)

TEST(Wait, simple)
{
    int c = 0;
    go [&]{
        EXPECT_EQ(c, 0);
        co_timer_add(seconds(1), [&]{
                EXPECT_EQ(c, 0);
                co_wakeup(1, 1);
        });

        auto s = system_clock::now();
        co_wait(1, 1);
        auto e = system_clock::now();
        auto ms = duration_cast<milliseconds>(e - s).count();
        EXPECT_LT(ms, 1100);
        EXPECT_GT(ms, 950);
        EXPECT_EQ(++c, 1);
    };
    co_sched.RunUntilNoTask();
}

TEST(Wait, overlapped)
{
    const int tc = 10000;
    for (int i = 0; i < tc; ++i)
        go [&]{
            co_timer_add(seconds(1), [&]{
                    co_wakeup(1, 1);
            });

            auto yn = g_Scheduler.GetCurrentTaskYieldCount();
            co_wait(1, 1);
            EXPECT_YIELD(yn + 1);
        };
    co_sched.RunUntilNoTask();
}

using ::testing::TestWithParam;
using ::testing::Values;
struct Wait_t : public TestWithParam<int>
{
    int tc;
    void SetUp() { tc = GetParam(); }
};

TEST_P(Wait_t, multi)
{
    for (int i = 0; i < tc; ++i)
        go [&, i]{
//            auto s = system_clock::now();
            co_wait(1, i);
//            auto e = system_clock::now();
//            auto ms = duration_cast<milliseconds>(e - s).count();
//            EXPECT_LT(ms, 1200);
//            EXPECT_GT(ms, 990);
        };

    go [=] {
//    co_timer_add(seconds(1), [&]{
                for (int i = 0; i < tc; ++i)
                    co_wakeup(1, i);
//            });
    };
    auto s = system_clock::now();
    co_sched.RunUntilNoTask();
    auto e = system_clock::now();
    auto ms = duration_cast<milliseconds>(e - s).count();
    printf("%d threads, do %d times, cost %d ms.\n", 1, tc, (int)ms);
}

TEST_P(Wait_t, nthread_multi)
{
    for (int i = 0; i < tc; ++i)
        go [&, i]{
//            auto s = system_clock::now();
            co_wait(1, i);
//            auto e = system_clock::now();
//            auto ms = duration_cast<milliseconds>(e - s).count();
//            if (ms > 1200)
//                printf("timeout index: %d\n", (int)i);
//            EXPECT_LT(ms, 1200);
//            EXPECT_GT(ms, 990);
        };

    go [=] {
//        co_timer_add(seconds(1), [&]{
                    for (int i = 0; i < tc; ++i)
                        co_wakeup(1, i);
//                });
    };

    auto s = system_clock::now();
    boost::thread_group tg;
    int thread_count = 8;
    for (int i = 0; i < thread_count; ++i)
        tg.create_thread([]{
                co_sched.RunUntilNoTask();
                });
    tg.join_all();
    auto e = system_clock::now();
    auto ms = duration_cast<milliseconds>(e - s).count();
    printf("%d threads, do %d times, cost %d ms.\n", thread_count, tc, (int)ms);
}

INSTANTIATE_TEST_CASE_P(
        CoWaitTest,
        Wait_t,
        Values(10000, 100000//, 400000
            ));
