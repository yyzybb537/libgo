#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include <time.h>
#include <poll.h>
#include "../gtest_exit.h"
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
//    co_opt.debug = dbg_timer;

    int c = 0, n = 10;
    for (int i = 0; i < n; ++i)
        go [&c, this]{
            do_sleep(type_, 0);
            ++c;
        };

    GTimer gt;
    WaitUntilNoTask();
    TIMER_CHECK(gt, 0, 10);

//    co_opt.debug = 0;
}

TEST_P(Sleep, sleep1)
{
    int c = 0, n = 2;
    for (int i = 0; i < n; ++i)
        go [&c, this]{
            do_sleep(type_, 1000);
            ++c;
        };

    GTimer gt;
    WaitUntilNoTask();
    TIMER_CHECK(gt, 1000, 100);
}

INSTANTIATE_TEST_CASE_P(
        SleepTypeTest,
        Sleep,
        Values(sleep_type::syscall_sleep, sleep_type::syscall_usleep, sleep_type::syscall_nanosleep,
            sleep_type::syscall_poll_0, sleep_type::syscall_poll));
