#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

TEST(Mutex, test_mutex)
{
//    co_sched.GetOptions().debug = dbg_switch;

    go []{
        std::cout << "start mutex test...\n" << std::endl;
    };
    WaitUntilNoTask();

    co_mutex m;

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

    int *pv = new int(0);
    std::vector<int> *vec = new std::vector<int>;
    vec->reserve(100 * 100);
    for (int i = 0; i < 100; ++i)
        go [&]()mutable {
            for (int i = 0; i < 100; ++i)
            {
                std::unique_lock<co_mutex> lock(m);
                vec->push_back(++*pv);
            }
        };
    WaitUntilNoTask();
    for (int i = 0; i < (int)vec->size(); ++i)
    {
        EXPECT_EQ(i + 1, vec->at(i));
    }
}

TEST(Mutex, test_rwmutex)
{
    co_rwmutex m;

    // reader view
    go [&]()mutable {
        EXPECT_FALSE(m.reader().is_lock());
        m.reader().lock();
        EXPECT_FALSE(m.reader().is_lock());
        m.reader().unlock();
        EXPECT_FALSE(m.reader().is_lock());

        EXPECT_TRUE(m.reader().try_lock());
        EXPECT_TRUE(m.reader().try_lock());
        EXPECT_TRUE(m.reader().try_lock());
        m.reader().unlock();
        m.reader().unlock();
        m.reader().unlock();
    };
    WaitUntilNoTask();

    // writer view
    go [&]()mutable {
        EXPECT_FALSE(m.writer().is_lock());
        m.writer().lock();
        EXPECT_TRUE(m.writer().is_lock());
        m.writer().unlock();
        EXPECT_FALSE(m.writer().is_lock());

        EXPECT_TRUE(m.writer().try_lock());
        EXPECT_FALSE(m.writer().try_lock());
        m.writer().unlock();
        EXPECT_FALSE(m.writer().is_lock());
    };
    WaitUntilNoTask();

    // cross two view
    go [&]()mutable {
        {
            std::unique_lock<co_wmutex> lock(m.writer());
            EXPECT_FALSE(m.reader().try_lock());
            EXPECT_FALSE(m.writer().try_lock());
        }

        {
            std::unique_lock<co_rmutex> lock(m.reader());
            EXPECT_TRUE(m.reader().try_lock());
            m.reader().unlock();
            EXPECT_FALSE(m.writer().try_lock());
            EXPECT_FALSE(m.writer().is_lock());
        }

        EXPECT_TRUE(m.writer().try_lock());
        EXPECT_TRUE(m.writer().is_lock());
        m.writer().unlock();
        EXPECT_FALSE(m.writer().is_lock());
    };
    WaitUntilNoTask();

    // multi threads
    int *v1 = new int(0);
    int *v2 = new int(0);

    go [&]()mutable {
        // write
        for (int i = 0; i < 10000; ++i)
        {
            std::unique_lock<co_wmutex> lock(m.writer());
            ++ *v1;
            ++ *v2;
        }
    };

    for (int i = 0; i < 100; ++i)
        go [&]()mutable {
            // read
            for (int i = 0; i < 10000; ++i)
            {
                std::unique_lock<co_rmutex> lock(m.reader());
                EXPECT_EQ(*v1, *v2);
            }
        };
    WaitUntilNoTask();
    EXPECT_EQ(*v1, 10000);
    EXPECT_EQ(*v2, 10000);
}
