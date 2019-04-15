#include "gtest/gtest.h"
#include <boost/thread.hpp>
#include <boost/timer.hpp>
#include <vector>
#include <list>
#include <atomic>
#include "coroutine.h"
#include "gtest_exit.h"
using namespace std::chrono;
using namespace co;

//#define EXPECT_YIELD(n) EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), size_t(n))
#define EXPECT_YIELD(n)

#define SLEEP(ms) \
    do {\
        Processer::Suspend(milliseconds(ms)); co_yield;\
    } while(0)

TEST(Channel, capacity0)
{
    co_chan<int> ch;
    EXPECT_TRUE(ch.empty());

    int i = 0;
    {
        EXPECT_EQ(ch.size(), 0u);
        go [&]{
            ch << 1;
            EXPECT_YIELD(1);
        };
        go [&]{
            ch >> i;
            EXPECT_YIELD(0);
        };
        WaitUntilNoTask();
        EXPECT_EQ(i, 1);

//    co_opt.debug = co::dbg_channel;
//    co_opt.debug_output = fopen("log", "w");

        EXPECT_EQ(ch.size(), 0u);
        go [=]{ ch << 2; EXPECT_YIELD(1);};
        go [=, &i]{ ch >> i; EXPECT_YIELD(0);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 2);

//    exit(0);

        EXPECT_EQ(ch.size(), 0u);
        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; ch >> i; after_pop = ++step; EXPECT_YIELD(0);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(ch.size(), 0u);
    }

    // ignore
    {
        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; ch >> nullptr; after_pop = ++step; EXPECT_YIELD(0);};
        WaitUntilNoTask();
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
    }

    // multi thread
    int k = 0;
    std::atomic<int> total{0};
    int total_check = 0;
    for (k = 0; k < 100; ++k) {
        total_check += k;
        go [=]{
            ch << k;
        };
        go [&]{
            int v;
            ch >> v;
            total += v;
        };
    }
    WaitUntilNoTask();
    EXPECT_EQ(total, total_check);
}

TEST(Channel, capacity1)
{
    // nonblock
    co_chan<int> ch(1);
    EXPECT_TRUE(ch.empty());
    int i = -1;

    {
        EXPECT_EQ(ch.size(), 0u);
        go [&]{
            ch << 1;
            EXPECT_FALSE(ch.empty());
            EXPECT_EQ(ch.size(), 1u);
            EXPECT_YIELD(0);
            go [&]{
                ch >> i;
                EXPECT_YIELD(0);
            };
        };
        WaitUntilNoTask();
        EXPECT_EQ(ch.size(), 0u);
        EXPECT_EQ(i, 1);

        go [=]{ ch << 2; EXPECT_YIELD(0);};
        go [=, &i]{ SLEEP(50); ch >> i; EXPECT_YIELD(1);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 2);
    }

    {
        // block
        ch << 0;
        go [&]{ ch << 1; EXPECT_YIELD(1);};
        go [&]{ SLEEP(100); ch >> i; EXPECT_YIELD(1);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 0);

        go [=]{ ch << 2; EXPECT_YIELD(1);};
        go [=, &i]{ SLEEP(100); ch >> i; EXPECT_YIELD(1);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 1);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ SLEEP(50); before_pop = ++step; ch >> i; after_pop = ++step; EXPECT_YIELD(1);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 2);
        EXPECT_EQ(before_push + before_pop, 3);
        EXPECT_EQ(after_pop + after_push, 7);

        EXPECT_TRUE(ch.TryPop(i));
        EXPECT_EQ(i, 3);
    }
}

TEST(Channel, capacityN)
{
    int n = 10;
    co_chan<int> ch(n);
    int i = 0;

    // nonblock
    {
        go [&]{ ch << 1; EXPECT_YIELD(0);};
        go [&]{ ch >> i; EXPECT_YIELD(0);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 1);

        go [=]{ ch << 2; EXPECT_YIELD(0);};
        go [=, &i]{ ch >> i; EXPECT_YIELD(0);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 2);
    }

    // block
    {
        for (int i = 0; i < n; ++i)
            ch << i;
        go [&]{ ch << n; EXPECT_YIELD(1);};
        go [&]{ SLEEP(100); ch >> i; EXPECT_YIELD(1);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 0);

        go [=]{ ch << n + 1; EXPECT_YIELD(1);};
        go [=, &i]{ SLEEP(100); ch >> i; EXPECT_YIELD(1);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 1);

        for (int i = 0; i < n; ++i) {
            int x;
            ch >> x;
            EXPECT_EQ(x, i + 2);
        }
    }
}

TEST(Channel, capacity0Try)
{
    co_chan<int> ch;
    int i = 0;
    // try pop
    if (0)
    {
        go [&]{ EXPECT_FALSE(ch.TryPop(i)); EXPECT_YIELD(0); SLEEP(100); EXPECT_TRUE(ch.TryPop(i)); EXPECT_YIELD(1); };
        go [&]{ SLEEP(50); ch << 1; EXPECT_YIELD(2);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 1);

        go [=]{ ch << 2; EXPECT_YIELD(1);};
        go [&]{ SLEEP(50); EXPECT_TRUE(ch.TryPop(i)); EXPECT_YIELD(1); };
        WaitUntilNoTask();
        EXPECT_EQ(i, 2);
    }

    // try push
    {
        go [&]{
            EXPECT_FALSE(ch.TryPush(1));
            go [&] { 
                SLEEP(50);
                EXPECT_TRUE(ch.TryPush(1));
            };
            ch >> i;
            EXPECT_YIELD(1);
        };
        WaitUntilNoTask();
        EXPECT_EQ(i, 1);

        go [&]{ ch >> i; EXPECT_YIELD(1);};
        go [&]{ SLEEP(50); EXPECT_TRUE(ch.TryPush(2)); EXPECT_YIELD(1); };
        WaitUntilNoTask();
        EXPECT_EQ(i, 2);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch >> i; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ SLEEP(50); before_pop = ++step; EXPECT_TRUE(ch.TryPush(3)); after_pop = ++step; EXPECT_YIELD(1);};
        WaitUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(after_pop, 3);
        EXPECT_EQ(after_push, 4);
    }

    // try push and pop
    {
        go [&]{
            for (int i = 0; i < 10000; ++i) {
                EXPECT_FALSE(ch.TryPush(1));
            }
        };
        go [&]{
            int j;
            for (int i = 0; i < 10000; ++i) {
                EXPECT_FALSE(ch.TryPop(j));
            }
        };
        WaitUntilNoTask();
    }
}

TEST(Channel, capacity1Try)
{
    co_chan<int> ch(1);
    int i = 0;
    // try pop
    {
        go [&]{ 
            EXPECT_FALSE(ch.TryPop(i));
            EXPECT_YIELD(0); 
            SLEEP(100); 
            EXPECT_TRUE(ch.TryPop(i)); 
            EXPECT_YIELD(1); 
        };
        go [&]{ 
            SLEEP(50); 
            ch << 1; 
            EXPECT_YIELD(1);
        };
        WaitUntilNoTask();
        EXPECT_EQ(i, 1);

        go [=]{ 
            ch << 2; 
            EXPECT_YIELD(0);
        };
        go [&]{ 
            SLEEP(50); 
            EXPECT_TRUE(ch.TryPop(i)); 
            EXPECT_YIELD(1); 
        };
        WaitUntilNoTask();
        EXPECT_EQ(i, 2);
    }

    // try push
    {
        ch << 0;
        go [&]{
            EXPECT_FALSE(ch.TryPush(1));
            EXPECT_YIELD(0);
            SLEEP(100);
            EXPECT_TRUE(ch.TryPush(1));
            EXPECT_YIELD(1);
        };

        go [&]{ SLEEP(50);
            ch >> i;
            EXPECT_YIELD(1);
        };

        WaitUntilNoTask();
        EXPECT_EQ(i, 0);

        go [&]{ ch >> i;
            EXPECT_YIELD(0);
        };

        go [&]{ SLEEP(50);
            EXPECT_TRUE(ch.TryPush(2));
            EXPECT_YIELD(1);
        };

        WaitUntilNoTask();
        EXPECT_EQ(i, 1);
    }
}

TEST(Channel, capacity0TimedTypes)
{
    {
        co_chan<int> ch;

        // block try
        go [=] {
            GTimer t;
            bool ok = ch.TimedPush(1, seconds(1));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 1000, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();
    }

    {
        co_chan<int> ch;

        // block try
        go [=] {
            GTimer t;
            bool ok = ch.TimedPush(1, milliseconds(32));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 32, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();
    }

    {
        co_chan<int> ch;

        // block try
        go [=] {
            GTimer t;
            bool ok = ch.TimedPush(1, std::chrono::milliseconds(32));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 32, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();
    }
}

TEST(Channel, capacity0Timed)
{
    {
        co_chan<int> ch;

        // block try
        go [=] {
            GTimer t;
            bool ok = ch.TimedPush(1, milliseconds(32));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 32, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();

        // block try
        go [=] {
            GTimer t;
            bool ok = ch.TimedPush(1, milliseconds(100));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 100, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();

        go [=] {
            GTimer t;
            int i;
            bool ok = ch.TimedPop(i, milliseconds(100));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 100, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();

        go [=] {
            GTimer t;
            bool ok = ch.TimedPop(nullptr, milliseconds(100));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 100, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();
    }

    {
        co_chan<void> ch;

        go [=] {
            GTimer t;
            bool ok = ch.TimedPush(nullptr, milliseconds(100));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 100, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();

        go [=] {
            GTimer t;
            bool ok = ch.TimedPop(nullptr, milliseconds(100));
            EXPECT_FALSE(ok);
            TIMER_CHECK(t, 100, DEFAULT_DEVIATION);
        };
        WaitUntilNoTask();
    }

    {
        co_chan<void> ch;

        for (int i = 0; i < 1000; ++i)
            go [=] {
                GTimer t;
                bool ok = ch.TimedPush(nullptr, milliseconds(500));
                EXPECT_FALSE(ok);
                TIMER_CHECK(t, 500, 100);
            };
        WaitUntilNoTask();
    }

    {
        co_chan<void> ch;

        for (int i = 0; i < 1000; ++i)
            go [=] {
                GTimer t;
                bool ok = ch.TimedPop(nullptr, milliseconds(500));
                EXPECT_FALSE(ok);
                TIMER_CHECK(t, 500, 100);
            };
        WaitUntilNoTask();
    }

    {
        co_chan<int> ch;
        int *p = new int[1000];

        for (int i = 0; i < 1000; ++i)
            go [=] {
                int v;
                GTimer t;
                bool ok = ch.TimedPop(v, milliseconds(500));
                EXPECT_TRUE(ok);
                p[v] = 1;
                TIMER_CHECK(t, 0, 200);
            };

        for (int i = 0; i < 1000; ++i)
            go [=] {
                ch << i;
            };

        WaitUntilNoTask();

        for (int i = 0; i < 1000; ++i) {
            EXPECT_EQ(p[i], 1);
        }
        delete[] p;
    }
}
