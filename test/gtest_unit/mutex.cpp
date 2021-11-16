#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
// #define OPEN_ROUTINE_SYNC_DEBUG 1
#include "coroutine.h"
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

co_mutex m;

TEST(Mutex, simple)
{
    // co::CoroutineOptions::getInstance().debug = dbg_rutex | dbg_mutex;
    // co::CoroutineOptions::getInstance().debug_output = fopen("a.log", "w");
    // co::CoroutineOptions::getInstance().debug = dbg_mutex;
    
    go [&]()mutable {
        EXPECT_FALSE(m.is_lock());
        m.lock();
        EXPECT_TRUE(m.is_lock());
        m.unlock();
        EXPECT_FALSE(m.is_lock());

        EXPECT_TRUE(m.try_lock());
        EXPECT_FALSE(m.try_lock());
        m.unlock();
        EXPECT_FALSE(m.is_lock());
    };
    WaitUntilNoTask();
}

TEST(Mutex, bench)
{
    int *pv = new int(0);
    std::vector<int> *vec = new std::vector<int>;
    const int c = 1000;
    const int c_coro = 1000;
    vec->reserve(c * c_coro);
    std::mutex thread_mutex;
    std::atomic_int reenter{0};
    for (int i = 0; i < c_coro; ++i)
        go [&]()mutable {
            for (int i = 0; i < c; ++i)
            {
                std::unique_lock<co_mutex> lock(m);
                EXPECT_EQ(++reenter, 1);
                vec->push_back(++*pv);
                EXPECT_EQ(--reenter, 0);            
            }

            DebugPrint(dbg_mutex, "done");
        };
    WaitUntilNoTask();
    for (int i = 0; i < (int)vec->size(); ++i)
    {
        EXPECT_EQ(i + 1, vec->at(i));
    }
}

