#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "test_server.h"
#include "coroutine.h"
#include <chrono>
#include <time.h>
#include <poll.h>
using namespace std;
using namespace co;

enum class sleep_type
{
    syscall_sleep,
    syscall_usleep,
    syscall_nanosleep,
    syscall_poll_0,
    syscall_poll,
};

void do_sleep(sleep_type type, int timeout)
{
    switch (type)
    {
        case sleep_type::syscall_sleep:
            sleep(timeout / 1000);
            break;

        case sleep_type::syscall_usleep:
            usleep(timeout * 1000);
            break;

        case sleep_type::syscall_nanosleep:
            {
                timespec tv{timeout / 1000, ((long int)timeout % 1000) * 1000000};
                nanosleep(&tv, NULL);
            }
            break;

        case sleep_type::syscall_poll_0:
            poll(NULL, 0, timeout);
            break;

        case sleep_type::syscall_poll:
            pollfd pf = {-1, 0, 0};
            poll(&pf, 1, timeout);
            break;
    }
}

using ::testing::TestWithParam;
using ::testing::Values;

struct Sleep : public TestWithParam<sleep_type>
{
    sleep_type type_;
    void SetUp() { type_ = GetParam(); }
};

TEST_P(Sleep, sleep0)
{
//    g_Scheduler.GetOptions().debug = dbg_sleepblock;

    int c = 0, n = 2;
    for (int i = 0; i < n; ++i)
        go [&c, this]{
            do_sleep(type_, 0);
            ++c;
        };

    auto s = chrono::system_clock::now();
    g_Scheduler.RunUntilNoTask();
    auto e = chrono::system_clock::now();
    auto dc = chrono::duration_cast<chrono::milliseconds>(e - s).count();
    EXPECT_EQ(c, n);
    EXPECT_LT(dc, 100);
}

TEST_P(Sleep, sleep1)
{
//    g_Scheduler.GetOptions().debug = dbg_sleepblock;

    int c = 0, n = 2;
    for (int i = 0; i < n; ++i)
        go [&c, this]{
            do_sleep(type_, 1030);
            ++c;
        };

    auto s = chrono::system_clock::now();
    g_Scheduler.RunUntilNoTask();
    auto e = chrono::system_clock::now();
    auto dc = chrono::duration_cast<chrono::milliseconds>(e - s).count();
    EXPECT_EQ(c, n);
    EXPECT_LT(dc, 1100);
    EXPECT_GT(dc, 999);
}

INSTANTIATE_TEST_CASE_P(
        SleepTypeTest,
        Sleep,
        Values(sleep_type::syscall_sleep, sleep_type::syscall_usleep, sleep_type::syscall_nanosleep,
            sleep_type::syscall_poll_0, sleep_type::syscall_poll));
