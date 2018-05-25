#include <iostream>
#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <chrono>
#include "gtest_exit.h"
#include <libgo/coroutine.h>
using namespace std;
using namespace co;

void foo(bool is_affinity) {
    uint32_t thread_id = co_sched.GetCurrentThreadID();
    for (int i = 0; i < 10; i++) {
        uint32_t cur = co_sched.GetCurrentThreadID();
//        printf("origin thread_id = %u, cur = %u\n", thread_id, cur);
        if (is_affinity) {
            EXPECT_EQ(thread_id, cur);
        }
        co_sleep(10);
    }
}

TEST(testAffinity, testAffinity)
{
    co_sched.GetOptions().enable_work_steal = true;

    for (int i = 0; i < 100; i++)
        go co_affinity() []{ foo(true); };

    for (int i = 0; i < 100; i++)
        go []{ foo(false); };

    boost::thread_group tg;
    for (int i = 0; i < 8; i++)
        tg.create_thread([]{
            co_sched.RunUntilNoTask();
            });
    tg.join_all();
}
