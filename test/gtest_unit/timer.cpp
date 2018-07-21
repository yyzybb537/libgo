#include "gtest/gtest.h"
#include <boost/thread.hpp>
#include "coroutine.h"
#include <vector>
#include <list>
#include <atomic>
#include <boost/timer.hpp>
#include "gtest_exit.h"
#include <iostream>
using namespace co;
using std::cout;
using std::endl;
using std::chrono::seconds;
using std::chrono::milliseconds;

co_timer timer(true);

/*
TEST(Timer, OnTime1)
{
    g_Scheduler.GetOptions().debug = dbg_timer;
    GTimer gtimer;
    int c = 1;
    co_chan<void> q(1);
    const int ms = 100;
    timer.ExpireAt(milliseconds(ms), [&]{
            --c;
            TIMER_CHECK(gtimer, ms, 20);
            q << nullptr;
            });

    q >> nullptr;
    EXPECT_EQ(c, 0);
    g_Scheduler.GetOptions().debug = dbg_none;
}
*/

TEST(Timer, OnTime2)
{
//    g_Scheduler.GetOptions().debug = dbg_timer | dbg_channel;
    GTimer gtimer;
    int c = 100;
    co_chan<void> q(c);
    const int ms = 100;
    for (int i = 0; i < c; i++)
        timer.ExpireAt(std::chrono::milliseconds(ms + i), [&, i]{
                TIMER_CHECK(gtimer, ms + i, 50);
                DebugPrint(g_Scheduler.GetOptions().debug, "q <- %d\n", i);
                q << nullptr;
                });
    for (int i = 0; i < c; i++) {
        q >> nullptr;
        DebugPrint(g_Scheduler.GetOptions().debug, "q -> %d\n", i);
    }
//    g_Scheduler.GetOptions().debug = dbg_none;
}

/*
TEST(Timer, OnTime3)
{
    GTimer gtimer;
    int c = 1000;
    co_chan<void> q(c);
    const int ms = 100;
    for (int i = 0; i < c; i++)
        timer.ExpireAt(std::chrono::milliseconds(ms), [&]{
                TIMER_CHECK(gtimer, ms, 20);
                q << nullptr;
                });
    for (int i = 0; i < c; i++)
        q >> nullptr;
}

using ::testing::TestWithParam;
using ::testing::Values;

struct ThreadsTimer : public TestWithParam<int> {
    int thread_count_;
    void SetUp() { thread_count_ = GetParam(); }
};

TEST_P(ThreadsTimer, OnTime4)
{
    g_Scheduler.GetOptions().debug = dbg_none;
    std::atomic<int> c{10000};
    for (float i = 0; i < c; i+=1)
        co_timer_add(std::chrono::seconds(1), [&, i]{
                --c;
                });

    auto start = std::chrono::system_clock::now();
    boost::thread_group tg;
    tg.create_thread([&c] {
        while (c)
            g_Scheduler.Run();
            });
    tg.join_all();
    EXPECT_LT(second_duration(start), 2.0);
}

INSTANTIATE_TEST_CASE_P(OnThreadsTimer, ThreadsTimer, Values(2, 4, 8));

TEST(ThreadsTimer, Cancel)
{
    g_Scheduler.GetOptions().debug = dbg_none;

    bool executed = false;
    auto timer_id = co_timer_add(std::chrono::seconds(0), [&]{
                executed = true;
            });
    g_Scheduler.Run();
    EXPECT_TRUE(executed);

    executed = false;
    timer_id = co_timer_add(std::chrono::seconds(0), [&]{
                executed = true;
            });
    EXPECT_TRUE(co_timer_cancel(timer_id));
    g_Scheduler.Run();
    EXPECT_FALSE(executed);

    executed = false;
    timer_id = co_timer_add(std::chrono::seconds(0), [&]{
                executed = true;
            });
    g_Scheduler.Run();
    EXPECT_FALSE(co_timer_cancel(timer_id));
    EXPECT_TRUE(executed);
}


TEST(Timer, resolution)
{
    g_Scheduler.GetOptions().debug = co::dbg_scheduler_sleep;

    bool stop = false;
    go [&] {
        cout << "add sleep 300 ms" << endl;
        co_sleep(300);
        cout << "add timer 327 ms" << endl;
        co_timer_add(std::chrono::milliseconds(327), [&]{
                    cout << "timer trigger" << endl;
                    stop = true;
                });
    };

    auto start = std::chrono::system_clock::now();
    while (!stop)
        g_Scheduler.Run();
    EXPECT_LT(second_duration(start), 0.627 + 0.005);   // 误差不超过5ms
}

TEST(Timer, wait_resolution)
{
    g_Scheduler.GetOptions().debug = co::dbg_scheduler_sleep;

#if !defined(_WIN32)
    go [] {
        int fds[2];
        socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
        pollfd pfd = {fds[0], POLLIN, 1};
        poll(&pfd, 1, 100);
    };
    co_sched.RunUntilNoTask();
#endif

    bool stop = false;
    go [&] {
        cout << "add sleep 300 ms" << endl;
        co_sleep(300);
        cout << "add timer 327 ms" << endl;
        co_timer_add(std::chrono::milliseconds(327), [&]{
                    cout << "timer trigger" << endl;
                    stop = true;
                });
    };

    auto start = std::chrono::system_clock::now();
    while (!stop)
        g_Scheduler.Run();
    EXPECT_LT(second_duration(start), 0.627 + 0.005);   // 误差不超过5ms
}
*/
