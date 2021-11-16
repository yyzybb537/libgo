#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
// #define OPEN_ROUTINE_SYNC_DEBUG 1
#include "coroutine.h"
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

#if !USE_ROUTINE_SYNC
TEST(RoutineSyncTimer, simple) {}
#else

using namespace libgo;
using namespace std::chrono;

TEST(RoutineSyncTimer, simple) 
{
    co_mutex mtx;
    co_condition_variable cv;

    RoutineSyncTimer::TimerId id;
    RoutineSyncTimer::getInstance().schedule(id, 
        RoutineSyncTimer::now() + milliseconds(17),
        [&] {
            std::unique_lock<co_mutex> lock(mtx);
            cv.notify_one();
        });

    GTimer t;
    std::unique_lock<co_mutex> lock(mtx);
    auto status = cv.wait_for(lock, milliseconds(5));
    EXPECT_EQ(status, std::cv_status::timeout);

    status = cv.wait_for(lock, milliseconds(20));
    EXPECT_EQ(status, std::cv_status::no_timeout);

    TIMER_CHECK(t, 17, 2);
}

TEST(RoutineSyncTimer, linear) 
{
    const int c = 10000;
    const int ms = 100;
    co_chan<void> ch;

    int v = 0;
    vector<RoutineSyncTimer::TimerId> ids(c);
    for (int i = 0; i < (int)ids.size(); ++i) {
        auto t = ms * i / c;
        RoutineSyncTimer::getInstance().schedule(ids[i], 
            RoutineSyncTimer::now() + milliseconds(t),
            [&, i] {
                EXPECT_EQ(v, i);
                ++v;

                if (v == (int)ids.size()) {
                    ch << nullptr;
                }
            });
    }

    GTimer t;
    ch >> nullptr;
    TIMER_CHECK(t, ms, DEFAULT_DEVIATION);
    EXPECT_EQ(v, (int)ids.size());
}


TEST(RoutineSyncTimer, random_insert) 
{
    const int c = 10000;
    const int ms = 1000;
    co_chan<void> ch;

    int v = 0;
    GTimer startSched;
    vector<RoutineSyncTimer::TimerId> ids(c);
    for (int i = 0; i < (int)ids.size(); ++i) {
        auto dur = rand() % ms;
        GTimer startT;
        RoutineSyncTimer::getInstance().schedule(ids[i], 
            RoutineSyncTimer::now() + milliseconds(dur),
            [&, startT, dur] () mutable {
                // TIMER_EQ_1(startT, t);
                TIMER_CHECK(startT, dur, 20);
                // printf("startT.ms()=%d\n", startT.ms());
                // printf("t=%d\n", t);

                ++v;
                if (v == (int)ids.size()) {
                    ch << nullptr;
                }
            });
    }
    printf("schedules cost: %d ms\n", (int)startSched.ms());

    GTimer t;
    ch >> nullptr;
    EXPECT_LT(t.ms(), ms + 2);
    EXPECT_EQ(v, (int)ids.size());
}

TEST(RoutineSyncTimer, bench) 
{
    const int c = 100000;
    int v = 0;
    co_chan<void> ch;
    vector<RoutineSyncTimer::TimerId> ids(c);
    auto abstime = RoutineSyncTimer::now() + milliseconds(500);

    GTimer add;
    GTimer trigger;
    for (int i = 0; i < (int)ids.size(); ++i) {
        RoutineSyncTimer::getInstance().schedule(ids[i], 
            abstime,
            [&] {
                ++v;

                if (v == 1) {
                    trigger.reset();
                }

                if (v == (int)ids.size()) {
                    ch << nullptr;
                }
            });
    }
    add.dumpBench(c, "timer schedule");

    GTimer t;
    ch >> nullptr;
    EXPECT_EQ(v, (int)ids.size());
    trigger.dumpBench(c, "timer trigger");
}
#endif