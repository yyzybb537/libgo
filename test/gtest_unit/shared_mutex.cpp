#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
// #define OPEN_ROUTINE_SYNC_DEBUG 1
#include "coroutine.h"
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

co_rwmutex m;

TEST(RWMutex, simple)
{
    // reader view
    go [&]()mutable {
        EXPECT_FALSE(m.reader().is_lock());
        m.reader().lock();
        EXPECT_TRUE(m.reader().is_lock());
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
}

TEST(RWMutex, write_first)
{
    // ----------- 写优先
    std::atomic_int v {};

    // 先锁定reader
    for (int i = 0; i < 2; ++i) {
        go [&]()mutable {
            std::unique_lock<co_rmutex> lock(m.reader());
            ++v;
            co_sleep(500);
        };
    }

    go [&] {
        co_sleep(20);
        EXPECT_EQ(v, 2);

        // 标记写锁
        std::unique_lock<co_wmutex> lock(m.writer());
        EXPECT_EQ(v, 2);
        v += 10;
        co_sleep(500);
    };

    go [&] {
        co_sleep(100);
        EXPECT_EQ(v, 2);

        // 读锁排队
        GTimer t;
        std::unique_lock<co_rmutex> lock(m.reader());
        EXPECT_EQ(v, 12);
        TIMER_CHECK(t, 900, DEFAULT_DEVIATION);
    };
    
    WaitUntilNoTask();

#if USE_ROUTINE_SYNC
    printf("sizeof(libgo::DebuggerId<int>)=%d\n", (int)sizeof(libgo::DebuggerId<int>));
    printf("sizeof(std::mutex)=%d\n", (int)sizeof(std::mutex));
    printf("sizeof(RutexBase)=%d\n", (int)sizeof(libgo::RutexBase));
    printf("sizeof(IntValue<int>)=%d\n", (int)sizeof(libgo::IntValue<int, false>));
    printf("sizeof(IntValue<int, pointer>)=%d\n", (int)sizeof(libgo::IntValue<int, true>));
    printf("sizeof(Rutex<int>)=%d\n", (int)sizeof(libgo::Rutex<int>));
    printf("sizeof(Rutex<long>)=%d\n", (int)sizeof(libgo::Rutex<long>));
    printf("sizeof(Rutex<long, true>)=%d\n", (int)sizeof(libgo::Rutex<long, true>));    
    printf("sizeof(co_condition_variable)=%d\n", (int)sizeof(co_condition_variable));
    printf("sizeof(co_mutex)=%d\n", (int)sizeof(co_mutex));
    printf("sizeof(SharedMutex)=%d\n", (int)sizeof(libgo::SharedMutex));
    printf("sizeof(co_rwmutex)=%d\n", (int)sizeof(co_rwmutex));
#endif
}

TEST(RWMutex, bench)
{
    // multi threads
    int *v1 = new int(0);
    int *v2 = new int(0);

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(3);

    go [&]()mutable {
        // write
        while (std::chrono::system_clock::now() < deadline)
        {
            co_sleep(1);
            std::unique_lock<co_wmutex> lock(m.writer());
            ++ *v1;
            ++ *v2;
        }
    };

    for (int i = 0; i < 100; ++i)
        go [&]()mutable {
            // read
            while (std::chrono::system_clock::now() < deadline)
            {
                std::unique_lock<co_rmutex> lock(m.reader());
                EXPECT_EQ(*v1, *v2);
            }
        };
    WaitUntilNoTask();
    EXPECT_EQ(*v1, *v2);
}
