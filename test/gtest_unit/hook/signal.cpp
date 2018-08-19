#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include <signal.h>
#include "coroutine.h"
#include "../gtest_exit.h"
using namespace std;

static int triggered = 0;

static void sig_handler(int signum)
{
    triggered = signum;
}

TEST(signaltest, signaltest)
{
#if WITH_SAFE_SIGNAL
    printf("Test with safe signal.\n");

    EXPECT_EQ(triggered, 0);
    pid_t pid = getpid();
    signal(SIGHUP, &sig_handler);
    int res = kill(pid, SIGHUP);
    EXPECT_EQ(res, 0);

    EXPECT_EQ(triggered, 0);
    co_sched.Run();
    EXPECT_EQ(triggered, SIGHUP);

    // double
    triggered = 0;
    res = kill(pid, SIGHUP);
    EXPECT_EQ(res, 0);
    res = kill(pid, SIGHUP);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(triggered, 0);
    co_sched.Run();
    EXPECT_EQ(triggered, SIGHUP);
#else
    EXPECT_EQ(triggered, 0);
    pid_t pid = getpid();
    signal(SIGHUP, &sig_handler);
    EXPECT_EQ(triggered, 0);
    int res = kill(pid, SIGHUP);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(triggered, SIGHUP);
#endif
}
