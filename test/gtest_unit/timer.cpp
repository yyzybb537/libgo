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

co_timer timer;

const int cMiss = 10;

TEST(Timer, OnTime1)
{
//    co_opt.debug = dbg_timer;
    GTimer gtimer;
    int c = 1;
    co_chan<void> q(1);
    const int ms = 100;
    timer.ExpireAt(milliseconds(ms), [&]{
            --c;
            TIMER_CHECK(gtimer, ms, cMiss);
            q << nullptr;
            });

    q >> nullptr;
    EXPECT_EQ(c, 0);
    co_opt.debug = dbg_none;
}

TEST(Timer, OnTime2)
{
//    co_opt.debug = dbg_timer | dbg_channel;
    GTimer gtimer;
    int c = 100;
    co_chan<void> q(c);
    const int ms = 100;
    for (int i = 0; i < c; i++)
        timer.ExpireAt(std::chrono::milliseconds(ms + i), [&, i]{
                TIMER_CHECK(gtimer, ms + i, cMiss);
                DebugPrint(co_opt.debug, "q <- %d\n", i);
                q << nullptr;
                });
    for (int i = 0; i < c; i++) {
        q >> nullptr;
        DebugPrint(co_opt.debug, "q -> %d\n", i);
    }
//    co_opt.debug = dbg_none;
}

TEST(Timer, OnTime3)
{
    GTimer gtimer;
    int c = 100;
    co_chan<void> q(c);
    const int ms = 100;
    for (int i = 0; i < c; i++)
        timer.ExpireAt(std::chrono::milliseconds(ms), [&]{
                TIMER_CHECK(gtimer, ms, 100);
                q << nullptr;
                });
    for (int i = 0; i < c; i++)
        q >> nullptr;
}

