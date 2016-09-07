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

#define EXPECT_YIELD(n) EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), size_t(n))

TEST(Channel, capacity0)
{
    co_chan<int> ch;
    EXPECT_TRUE(ch.empty());

    int i = 0;
    {
        EXPECT_EQ(ch.size(), 0u);
        go [&]{ ch << 1; EXPECT_YIELD(1);};
        go [&]{ ch >> i; EXPECT_YIELD(1);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);

        EXPECT_EQ(ch.size(), 0u);
        go [=]{ ch << 2; EXPECT_YIELD(1);};
        go [=, &i]{ ch >> i; EXPECT_YIELD(1);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);

        EXPECT_EQ(ch.size(), 0u);
        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; ch >> i; after_pop = ++step; EXPECT_YIELD(1);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(after_push, 3);
        EXPECT_EQ(after_pop, 4);
        EXPECT_EQ(ch.size(), 0u);
    }

    // ignore
    {
        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; ch >> nullptr; after_pop = ++step; EXPECT_YIELD(1);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(after_push, 3);
        EXPECT_EQ(after_pop, 4);
    }

    // multi thread
    int k = 0;
    std::atomic<int> total{0};
    int total_check = 0;
    for (k = 0; k < 100; ++k) {
        total_check += k;
        go [=]{ ch << k; };
        go [&]{ short v; ch >> v; total += v; };
    }
    boost::thread_group tg;
    for (int t = 0; t < 8; ++t) {
        tg.create_thread([]{g_Scheduler.RunUntilNoTask();});
    }
    tg.join_all();
    EXPECT_EQ(total, total_check);
}

TEST(Channel, capacity1)
{
    // nonblock
    co_chan<int> ch(1);
    EXPECT_TRUE(ch.empty());
    int i = 0;
    {
        EXPECT_EQ(ch.size(), 0u);
        go [&]{ ch << 1; EXPECT_FALSE(ch.empty()); EXPECT_EQ(ch.size(), 1u); EXPECT_YIELD(0);};
        go [&]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(ch.size(), 0u);
        EXPECT_EQ(i, 1);

        go [=]{ ch << 2; EXPECT_YIELD(0);};
        go [=, &i]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(0);};
        go [&]{ before_pop = ++step; ch >> i; after_pop = ++step; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(after_push, 2);
        EXPECT_EQ(before_pop, 3);
        EXPECT_EQ(after_pop, 4);
    }

    {
        // block
        ch << 0;
        go [&]{ ch << 1; EXPECT_YIELD(1);};
        go [&]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 0);

        go [=]{ ch << 2; EXPECT_YIELD(1);};
        go [=, &i]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);
        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; ch >> i; after_pop = ++step; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(after_pop, 3);
        EXPECT_EQ(after_push, 4);

        ch >> i;
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
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);

        go [=]{ ch << 2; EXPECT_YIELD(0);};
        go [=, &i]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(0);};
        go [&]{ before_pop = ++step; ch >> i; after_pop = ++step; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(after_push, 2);
        EXPECT_EQ(before_pop, 3);
        EXPECT_EQ(after_pop, 4);
    }

    // block
    {
        for (int i = 0; i < n; ++i)
            ch << i;
        go [&]{ ch << n; EXPECT_YIELD(1);};
        go [&]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 0);

        go [=]{ ch << n + 1; EXPECT_YIELD(1);};
        go [=, &i]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);
        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << n + 2; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; ch >> i; after_pop = ++step; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(after_pop, 3);
        EXPECT_EQ(after_push, 4);

        for (int i = 0; i < n; ++i) {
            int x;
            ch >> x;
            EXPECT_EQ(x, i + 3);
        }
    }
}

TEST(Channel, capacity0Try)
{
    co_chan<int> ch;
    int i = 0;
    // try pop
    {
        go [&]{ EXPECT_FALSE(ch.TryPop(i)); EXPECT_YIELD(0); co_yield; EXPECT_TRUE(ch.TryPop(i)); EXPECT_YIELD(2); };
        go [&]{ ch << 1; EXPECT_YIELD(1);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);

        go [=]{ ch << 2; EXPECT_YIELD(1);};
        go [&]{ EXPECT_TRUE(ch.TryPop(i)); EXPECT_YIELD(1); };
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; EXPECT_TRUE(ch.TryPop(i)); after_pop = ++step; EXPECT_YIELD(1);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(after_push, 3);
        EXPECT_EQ(after_pop, 4);
    }

    // try push
    {
        go [&]{ EXPECT_FALSE(ch.TryPush(1)); EXPECT_YIELD(0); co_yield; EXPECT_TRUE(ch.TryPush(1)); EXPECT_YIELD(1); };
        go [&]{ ch >> i; EXPECT_YIELD(1);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);

        go [&]{ ch >> i; EXPECT_YIELD(1);};
        go [&]{ EXPECT_TRUE(ch.TryPush(2)); EXPECT_YIELD(0); };
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch >> i; after_push = ++step; EXPECT_YIELD(1);};
        go [&]{ before_pop = ++step; EXPECT_TRUE(ch.TryPush(3)); after_pop = ++step; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(before_pop, 2);
        EXPECT_EQ(after_pop, 3);
        EXPECT_EQ(after_push, 4);
    }

    // try push and pop
    {
        std::atomic<int> step{0};
        int push_done = 0, pop_done = 0, wake = 0, wake_done = 0;
        go [&]{
            for (;;) {
                if (ch.TryPush(7))
                    break;
                co_yield;
            }
            push_done = ++step;
        };
        go [&]{
            for (;;) {
                if (ch.TryPop(i))
                    break;
                co_yield;
            }
            pop_done = ++step;
        };
        auto s = system_clock::now();
        co_timer_add(milliseconds(200), [&]{
                    go [&] {
                        wake = ++step;
                        ch << 7;
                        co_yield;
                        ch >> nullptr;
                        wake_done = ++step;
                    };
                });
        g_Scheduler.RunUntilNoTask();
        auto e = system_clock::now();
        auto d = duration_cast<milliseconds>(e - s).count();
        EXPECT_LT(d, 250);
        EXPECT_GT(d, 190);
        EXPECT_EQ(i, 7);
        EXPECT_EQ(wake, 1);
        EXPECT_EQ(pop_done, 2);
        EXPECT_EQ(push_done, 3);
        EXPECT_EQ(wake_done, 4);
    }
}

TEST(Channel, capacity1Try)
{
    co_chan<int> ch(1);
    int i = 0;
    // try pop
    {
        go [&]{ EXPECT_FALSE(ch.TryPop(i)); EXPECT_YIELD(0); co_yield; EXPECT_TRUE(ch.TryPop(i)); EXPECT_YIELD(1); };
        go [&]{ ch << 1; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);

        go [=]{ ch << 2; EXPECT_YIELD(0);};
        go [&]{ EXPECT_TRUE(ch.TryPop(i)); EXPECT_YIELD(0); };
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch << 3; after_push = ++step; EXPECT_YIELD(0);};
        go [&]{ before_pop = ++step; EXPECT_TRUE(ch.TryPop(i)); after_pop = ++step; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 3);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(after_push, 2);
        EXPECT_EQ(before_pop, 3);
        EXPECT_EQ(after_pop, 4);
    }

    // try push
    {
        ch << 0;
        go [&]{ EXPECT_FALSE(ch.TryPush(1)); EXPECT_YIELD(0); co_yield; EXPECT_TRUE(ch.TryPush(1)); EXPECT_YIELD(1); };
        go [&]{ ch >> i; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 0);

        go [&]{ ch >> i; EXPECT_YIELD(0);};
        go [&]{ EXPECT_TRUE(ch.TryPush(2)); EXPECT_YIELD(0); };
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 1);

        std::atomic<int> step{0};
        int before_push = 0, after_push = 0, before_pop = 0, after_pop = 0;
        go [&]{ before_push = ++step; ch >> i; after_push = ++step; EXPECT_YIELD(0);};
        go [&]{ before_pop = ++step; EXPECT_TRUE(ch.TryPush(3)); after_pop = ++step; EXPECT_YIELD(0);};
        g_Scheduler.RunUntilNoTask();
        EXPECT_EQ(i, 2);
        EXPECT_EQ(before_push, 1);
        EXPECT_EQ(after_push, 2);
        EXPECT_EQ(before_pop, 3);
        EXPECT_EQ(after_pop, 4);
        ch >> i;
        EXPECT_EQ(i, 3);
    }
}

TEST(Channel, capacity0TimedTypes)
{
    {
        co_chan<int> ch;

        // block try
        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPush(1, seconds(1));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 999);
            EXPECT_LT(c, 1064);
        };
        g_Scheduler.RunUntilNoTask();
    }

    {
        co_chan<int> ch;

        // block try
        auto deadline = std::chrono::system_clock::now() + milliseconds(32);
        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPush(1, deadline);
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 30);
            EXPECT_LT(c, 64);
        };
        g_Scheduler.RunUntilNoTask();
    }

    {
        co_chan<int> ch;

        // block try
        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPush(1, std::chrono::duration_cast<MininumTimeDurationType>(milliseconds(32)));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 31);
            EXPECT_LT(c, 64);
        };
        g_Scheduler.RunUntilNoTask();
    }
}

TEST(Channel, capacity0Timed)
{
    {
        co_chan<int> ch;

        // block try
        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPush(1, milliseconds(32));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 31);
            EXPECT_LT(c, 64);
        };
        g_Scheduler.RunUntilNoTask();

        // block try
        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPush(1, milliseconds(100));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 99);
            EXPECT_LT(c, 133);
        };
        g_Scheduler.RunUntilNoTask();

        go [=] {
            auto s = system_clock::now();
            int i;
            bool ok = ch.TimedPop(i, milliseconds(100));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 99);
            EXPECT_LT(c, 133);
        };
        g_Scheduler.RunUntilNoTask();

        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPop(nullptr, milliseconds(100));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 99);
            EXPECT_LT(c, 133);
        };
        g_Scheduler.RunUntilNoTask();
    }

    {
        co_chan<void> ch;

        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPush(nullptr, milliseconds(100));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 99);
            EXPECT_LT(c, 133);
        };
        g_Scheduler.RunUntilNoTask();

        go [=] {
            auto s = system_clock::now();
            bool ok = ch.TimedPop(nullptr, milliseconds(100));
            auto e = system_clock::now();
            auto c = duration_cast<milliseconds>(e - s).count();
            EXPECT_FALSE(ok);
            EXPECT_GT(c, 99);
            EXPECT_LT(c, 133);
        };
        g_Scheduler.RunUntilNoTask();
    }

    {
        co_chan<void> ch;

        for (int i = 0; i < 1000; ++i)
            go [=] {
                auto s = system_clock::now();
                bool ok = ch.TimedPush(nullptr, milliseconds(500));
                auto e = system_clock::now();
                auto c = duration_cast<milliseconds>(e - s).count();
                EXPECT_FALSE(ok);
                EXPECT_GT(c, 499);
                EXPECT_LT(c, 533);
            };
        g_Scheduler.RunUntilNoTask();
    }

    {
        co_chan<void> ch;

        for (int i = 0; i < 1000; ++i)
            go [=] {
                auto s = system_clock::now();
                bool ok = ch.TimedPop(nullptr, milliseconds(500));
                auto e = system_clock::now();
                auto c = duration_cast<milliseconds>(e - s).count();
                EXPECT_FALSE(ok);
                EXPECT_GT(c, 499);
                EXPECT_LT(c, 533);
            };
        g_Scheduler.RunUntilNoTask();
    }

    {
        co_chan<int> ch;

        for (int i = 0; i < 1000; ++i)
            go [=] {
                int v;
                auto s = system_clock::now();
                bool ok = ch.TimedPop(v, milliseconds(500));
                auto e = system_clock::now();
                auto c = duration_cast<milliseconds>(e - s).count();
                EXPECT_TRUE(ok);
                EXPECT_EQ(i, v);
                EXPECT_LT(c, 100);
            };

        for (int i = 0; i < 1000; ++i)
            go [=] {
                ch << i;
            };

        g_Scheduler.RunUntilNoTask();
    }
}
