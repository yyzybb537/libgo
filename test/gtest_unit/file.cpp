#include "test_server.h"
#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/file.h>
#include <chrono>
#include "coroutine.h"
using namespace std;
using namespace co;

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
        EXPECT_EQ(co_sched.GetCurrentTaskYieldCount(), 0);
        close(fd);
        fd = open("/dev/zero", O_RDWR);
        n = read(fd, buf, buf_len);
        EXPECT_EQ(n, buf_len);
        EXPECT_EQ(co_sched.GetCurrentTaskYieldCount(), 0);
    };
    co_sched.RunUntilNoTask();
}
