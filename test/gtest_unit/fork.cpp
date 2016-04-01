#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
using namespace std;
using namespace co;

void foo()
{
    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_DGRAM, 0, fds);
    EXPECT_EQ(res, 0);

    pollfd pfd;
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    res = poll(&pfd, 1, 1);
    EXPECT_EQ(errno, 0);
    EXPECT_EQ(res, 0);
}

TEST(forktest, forktest)
{
//    co_sched.GetOptions().debug = dbg_ioblock | dbg_hook;
//    co_sched.GetOptions().debug_output = fopen("log.fork", "a+");

    pid_t pid = fork();
    if (pid == 0)
        exit(0);

    EXPECT_GT(pid, 0);

    go &foo;
    co_sched.RunUntilNoTask();

    pid = fork();
    EXPECT_TRUE(pid >= 0);

    go &foo;
    co_sched.RunUntilNoTask();
}
