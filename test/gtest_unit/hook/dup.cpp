#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include "../gtest_exit.h"
using namespace std;
using namespace co;

void CheckPair(int fd1, int fd2)
{
    char buf = 'a';
    char rbuf = 0;
    int res = write(fd1, &buf, 1);
    EXPECT_EQ(res, 1);

    res = read(fd2, &rbuf, 1);
    EXPECT_EQ(res, 1);

    EXPECT_EQ(rbuf, buf);
}

TEST(testDup, testDup)
{
    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    CheckPair(fds[0], fds[1]);

    // dup
    int fd_dup = dup(fds[0]);
    EXPECT_TRUE(fd_dup > 0);
    CheckPair(fd_dup, fds[1]);

    // dup2
    res = dup2(fds[1], fd_dup);
    EXPECT_EQ(res, fd_dup);
    CheckPair(fd_dup, fds[0]);

#if defined(LIBGO_SYS_Linux)
    // dup3
    res = dup3(fds[0], fd_dup, O_CLOEXEC);
    if (res == -1) {
        // old system
        if (errno == EPERM)
            printf("Old System, unsupport dup3.\n");
        else
            printf("errno=%d\n", errno);
    } else {
        CheckPair(fd_dup, fds[1]);
    }
#endif
}
