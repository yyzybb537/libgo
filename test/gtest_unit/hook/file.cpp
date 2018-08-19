#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <chrono>
#include "libgo.h"
#include "../gtest_exit.h"
using namespace std;
using namespace co;

bool is_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    return (flags & O_NONBLOCK);
}

// 对文件fd有正确地行为
TEST(HOOK, filefd)
{
//    co_sched.GetOptions().debug = dbg_all;
    go []{
        int fd = open("/dev/null", O_RDWR);
        int buf_len = 1024 * 1024 * 10;
        char *buf = (char*)malloc(buf_len);
        ssize_t n = write(fd, buf, buf_len);
        EXPECT_EQ(n, buf_len);
        EXPECT_EQ(co_sched.GetCurrentTaskYieldCount(), 0u);
        close(fd);
        fd = open("/dev/zero", O_RDWR);
        n = read(fd, buf, buf_len);
        EXPECT_EQ(n, buf_len);
        EXPECT_EQ(co_sched.GetCurrentTaskYieldCount(), 0u);

        EXPECT_FALSE(is_nonblock(fd));

        int nonblock = 1;
        EXPECT_EQ(0, ioctl(fd, FIONBIO, &nonblock));

        EXPECT_TRUE(is_nonblock(fd));

        int flags = fcntl(fd, F_GETFL);
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

        EXPECT_FALSE(is_nonblock(fd));

        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        EXPECT_TRUE(is_nonblock(fd));
    };

    WaitUntilNoTask();
}
