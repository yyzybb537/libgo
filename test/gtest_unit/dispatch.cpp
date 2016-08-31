#include <stdio.h>
#include <iostream>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

TEST(testDispatch, testDispatch)
{
    // dispatch local thread
    for (int i = 0; i < 10; ++i)
        go []{
            EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);

            go_dispatch(egod_local_thread) []{
                EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);
            };
        };
    co_sched.RunUntilNoTask();

    co_sched.GetOptions().enable_work_steal = false;

    // dispatch robin thread
    for (int i = 0; i < 12; ++i)
        go []{
            EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);

            go_dispatch(egod_local_thread) []{
                EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);
            };
        };
    co_sched.RunUntilNoTask();

    // dispatch random thread from 0 to 0
    for (int i = 0; i < 10; ++i)
        go_dispatch(egod_random) []{
            EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);
        };
    co_sched.RunUntilNoTask();

    // dispatch robin from 0 to 0
    for (int i = 0; i < 12; ++i)
        go_dispatch(egod_robin) []{
            EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);
        };
    co_sched.RunUntilNoTask();

    // dispatch 0->3
    for (int i = 0; i < 12; ++i)
        go_dispatch(egod_robin) []{
            EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);

            go_dispatch(egod_local_thread) [=]{
                EXPECT_EQ(co_sched.GetCurrentThreadID(), 0u);
            };
        };
    for (int i = 0; i < 10; ++i)
        go_dispatch(i % 4) [=]{
            size_t tid = i % 4;
            EXPECT_EQ(co_sched.GetCurrentThreadID(), tid);

            go_dispatch(egod_local_thread) [=]{
                EXPECT_EQ(co_sched.GetCurrentThreadID(), tid);
            };
        };
    for (int i = 0; i < 12; ++i)
        go_dispatch(egod_robin) [i]{
            size_t tid = i % 4;
//            printf("tid:%d\n", tid);
            EXPECT_EQ(co_sched.GetCurrentThreadID(), tid);

            go_dispatch(egod_local_thread) [=]{
                EXPECT_EQ(co_sched.GetCurrentThreadID(), tid);
            };
        };
    for (int i = 0; i < 24; ++i)
        go_dispatch(egod_robin) [=]{
            size_t tid = i % 4;
            EXPECT_EQ(co_sched.GetCurrentThreadID(), tid);

            go_dispatch(egod_local_thread) [=]{
                EXPECT_EQ(co_sched.GetCurrentThreadID(), tid);
            };
        };
    for (int i = 0; i < 30; ++i)
        go_dispatch(egod_random) []{
            printf("egod_random runs in thread[%u]\n", co_sched.GetCurrentThreadID());
        };
    boost::thread_group tg;
    for (int i = 0; i < 3; ++i)
        tg.create_thread([]{ co_sched.RunUntilNoTask(); });
    co_sched.RunUntilNoTask();
    tg.join_all();
}
