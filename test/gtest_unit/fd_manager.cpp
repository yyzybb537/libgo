#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <fstream>
#include <stdio.h>
using namespace std;
using namespace co;

void foo()
{
//    int fd = open("/dev/null", O_WRONLY);
//    write(fd, "1", 1);
//    close(fd);

    std::ofstream ofs("/dev/zero");
    ofs << "123";
    ofs.close();

    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_DGRAM, 0, fds);
    EXPECT_EQ(res, 0);

    pollfd pfd;
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    res = poll(&pfd, 1, 1000);
    EXPECT_EQ(errno, 0);
    EXPECT_EQ(res, 0);
}

void foo2()
{
    FILE * f = fopen("/dev/zero", "r");
    fwrite("a", 1, 1, f);
    fclose(f);

    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_DGRAM, 0, fds);
    EXPECT_EQ(res, 0);

    pollfd pfd;
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    res = poll(&pfd, 1, 1000);
    EXPECT_EQ(errno, 0);
    EXPECT_EQ(res, 0);
}

TEST(fdmanager, fdmanager)
{
    go foo;
    co_sched.RunUntilNoTask();

    go foo2;
    co_sched.RunUntilNoTask();
}
