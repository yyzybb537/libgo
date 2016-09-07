#include <iostream>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

TEST(stat, stat)
{
    co_sched.GetOptions().enable_coro_stat = true;
    go []{};

    for (int i = 0; i < 2; ++i)
        go []{
            co_sleep(10000);
        };

    auto m = co::Task::GetStatInfo();
    EXPECT_EQ(m.size(), 2u);
    for (auto &kv : m)
    {
        printf("location:%s, count:%u\n", kv.first.to_string().c_str(), kv.second);
    }

    co_sched.Run();
    printf("------- run once -------\n");

    {
        auto m = co::Task::GetStatInfo();
        EXPECT_EQ(m.size(), 1u);
        for (auto &kv : m)
        {
            printf("location:%s, count:%u\n", kv.first.to_string().c_str(), kv.second);
        }
    }

    printf("AllInfo:\n%s\n", co_debugger.GetAllInfo().c_str());
}
