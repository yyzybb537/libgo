#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include "test_server.h"
#include "coroutine.h"
using namespace std;
using namespace co;

void foo()
{
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == socketfd) {
        perror("socket init error:");
        return ;
    }

    struct timeval rcvtimeout = {1, 0};
    if (-1 == setsockopt(socketfd, SOL_SOCKET,
                SO_RCVTIMEO, &rcvtimeout, sizeof(rcvtimeout)))
    {
        perror("setsockopt error:");
        return ;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(43222);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (-1 == connect(socketfd, (sockaddr*)&addr, sizeof(addr))) {
        perror("connect error:");
        return ;
    }

    uint32_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    char buf[1024] = {};
    auto start = std::chrono::high_resolution_clock::now();
    ssize_t n = read(socketfd, buf, sizeof(buf));
    auto end = std::chrono::high_resolution_clock::now();
    auto milli = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    EXPECT_LT(milli, 1100);
    EXPECT_GT(milli, 999);
}

TEST(IOTimed, Main)
{
//    g_Scheduler.GetOptions().debug = dbg_hook;
    go foo;
    g_Scheduler.RunUntilNoTask();
}

