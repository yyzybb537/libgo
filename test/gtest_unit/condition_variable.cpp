#include "gtest/gtest.h"
#include <boost/thread.hpp>
#include <boost/timer.hpp>
#include <vector>
#include <list>
#include <atomic>
#define OPEN_ROUTINE_SYNC_DEBUG 1
// #define TEST_MIN_THREAD 1
#include "coroutine.h"
#include "gtest_exit.h"
using namespace std::chrono;
using namespace co;

#if !USE_ROUTINE_SYNC
TEST(CondV, simple) {}
#else

co_condition_variable cv;
co_mutex mtx;
co_mutex mtx2;

TEST(CondV, notify_one)
{
    // co_opt.debug = dbg_channel | dbg_rutex | dbg_mutex;
    // co_opt.debug_output = fopen("a.log", "w");

    go [&] {
        std::unique_lock<co_mutex> lock(mtx);
        cv.wait(lock);
    };
    go [&] {
        co_sleep(20);
        std::unique_lock<co_mutex> lock(mtx);
        int res = cv.notify_one();
        EXPECT_EQ(res, 1);
    };
    WaitUntilNoTask();
}

TEST(CondV, wait_timeout)
{
    go [&] {
        GTimer t;
        std::unique_lock<co_mutex> lock(mtx);
        std::cv_status status = cv.wait_for(lock, milliseconds(110));
        EXPECT_EQ(status, std::cv_status::timeout);
        TIMER_CHECK(t, 110, DEFAULT_DEVIATION);
    };
    WaitUntilNoTask();
}

TEST(CondV, empty_notify)
{
    go [&] {
        {
            std::unique_lock<co_mutex> lock(mtx);
            int res = cv.notify_one();
            EXPECT_EQ(res, 0);
        }

        {
            std::unique_lock<co_mutex> lock(mtx);
            int res = cv.notify_all();
            EXPECT_EQ(res, 0);
        }

        co_sleep(200);

        {
            std::unique_lock<co_mutex> lock(mtx);
            int res = cv.notify_one();
            EXPECT_EQ(res, 1);
        }
    };
    go [&] {
        co_sleep(100);

        GTimer t;
        {
            std::unique_lock<co_mutex> lock(mtx);
            cv.wait(lock);
        }
        TIMER_CHECK(t, 100, DEFAULT_DEVIATION);
    };
    WaitUntilNoTask();
}

TEST(CondV, notify_all)
{
    // co_opt.debug = dbg_channel | dbg_rutex | dbg_mutex;
    // co_opt.debug_output = fopen("a.log", "w");

    go [&] {
        std::unique_lock<co_mutex> lock(mtx);
        cv.wait(lock);
    };
    go [&] {
        co_sleep(20);
        std::unique_lock<co_mutex> lock(mtx);
        int res = cv.notify_all();
        EXPECT_EQ(res, 1);
    };
    WaitUntilNoTask();

    for (int i = 0; i < 100; i++)
        go [&] {
            std::unique_lock<co_mutex> lock(mtx);
            cv.wait(lock);
        };

    go [&] {
        co_sleep(20);
        std::unique_lock<co_mutex> lock(mtx);
        int res = cv.notify_all();
        EXPECT_EQ(res, 100);
    };
    WaitUntilNoTask();
}

TEST(CondV, fast_notify_all)
{
    // co_opt.debug = dbg_channel | dbg_rutex | dbg_mutex;
    // co_opt.debug_output = fopen("a.log", "w");

    go [&] {
        std::unique_lock<co_mutex> lock(mtx);
        cv.wait(lock);
    };
    go [&] {
        co_sleep(20);
        std::unique_lock<co_mutex> lock(mtx);
        cv.fast_notify_all(lock);
    };
    WaitUntilNoTask();

    for (int i = 0; i < 100; i++)
        go [&] {
            std::unique_lock<co_mutex> lock(mtx);
            cv.wait(lock);
        };

    go [&] {
        co_sleep(20);
        std::unique_lock<co_mutex> lock(mtx);
        cv.fast_notify_all(lock);
    };
    WaitUntilNoTask();
}

TEST(CondV, bench1)
{
    // co_opt.debug = dbg_rutex | dbg_mutex | dbg_cond_v;
    // co_opt.debug_output = fopen("a.log", "w");

    const int c = 1000;
    std::atomic_bool done{false};
    long notify_count = 0;
    go [&] {
        for (int i = 0; i < c; i++) {
            std::unique_lock<co_mutex> lock(mtx);
            cv.wait(lock);
            RS_DBG(dbg_all, "gtest wait success %d", i + 1);
        }
        done = true;
    };

    go [&] {
        while (!done) {
            {
                std::unique_lock<co_mutex> lock(mtx);
                cv.notify_one();
                ++notify_count;
            }

            co_yield;
        }
    };
    WaitUntilNoTask();
#if TEST_MIN_THREAD == 1 && TEST_MAX_THREAD == 1
    EXPECT_EQ(notify_count, c);
#endif
    printf("notify_count=%ld\n", notify_count);
}

TEST(CondV, bench2_queue)
{
    // co_opt.debug = dbg_rutex | dbg_mutex | dbg_cond_v;
    // co_opt.debug = dbg_test;
    // co_opt.debug_output = fopen("a.log", "w");

    int val {0};
    const int c = 100000;
    const int consumer = 10;
    const int producer = 10;
    std::deque<int> dq;
    std::atomic_long c_acc {}, p_acc {};

    for (int j = 0; j < consumer; j++) {
        go [&] {
            for (;;) {
                std::unique_lock<co_mutex> lock(mtx);
                while (dq.empty()) {
                    if (val > c) {
                        return ;
                    }

                    cv.wait_for(lock, milliseconds(1000));
                }
                    
                c_acc += dq.front();
                dq.pop_front();
            }
        };
    }

    for (int j = 0; j < producer; j++){
        go [&] {
            for (;;) {
                {
                    std::unique_lock<co_mutex> lock(mtx);
                    if (val > c) {
                        return ;
                    }

                    int v = ++val;
                    p_acc += v;
                    dq.push_back(v);
                    cv.notify_one();
                }

                co_yield;
            }
        };
    }
    
    WaitUntilNoTask();
    EXPECT_EQ(p_acc, c_acc);
}

#endif