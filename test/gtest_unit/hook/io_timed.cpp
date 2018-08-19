#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include "netio/unix/hook.h"
#include <sys/fcntl.h>
#include <sys/types.h>
#include "../gtest_exit.h"
using namespace std;
using namespace co;

static int fill_send_buffer(int fd)
{
    static char* buf = new char[1024];
    int c = 0;
    for (;;) {
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 100) <= 0)
            break;
        c += write(fd, buf, 1024);
    }
    return c;
}

void timed()
{
    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    int socketfd = fds[0];

    timeval rcvtimeout = {1, 100 * 1000};
    res = setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &rcvtimeout, sizeof(rcvtimeout));
    EXPECT_EQ(res, 0);

    timeval sndtimeout = {0, 500 * 1000};
    res = setsockopt(socketfd, SOL_SOCKET, SO_SNDTIMEO, &sndtimeout, sizeof(sndtimeout));
    EXPECT_EQ(res, 0);

    res = fill_send_buffer(socketfd);
    cout << "fill " << res << " bytes on write buffer." << endl;

    char buf[1024] = {};
    uint32_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    GTimer gt;
retry:
    gt.reset();
    ssize_t n = write(socketfd, buf, sizeof(buf));
    if (n > 0) {
        res += n;
        goto retry;
    }
    cout << "fill " << res << " bytes on write buffer." << endl;
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    TIMER_CHECK(gt, 500, 50);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    gt.reset();
    n = read(socketfd, buf, sizeof(buf));
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    TIMER_CHECK(gt, 1100, 50);

    // set nonblock
    int flags = fcntl(socketfd, F_GETFL);
    res = fcntl(socketfd, F_SETFL, flags | O_NONBLOCK);
    EXPECT_EQ(res, 0);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    gt.reset();
    n = read(socketfd, buf, sizeof(buf));
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count);
    TIMER_CHECK(gt, 0, 30);
}

void connect_timeo()
{
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GT(socketfd, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(23);
    addr.sin_addr.s_addr = inet_addr("8.8.8.0");

    // set connect timeout is 300 milliseconds.
    ::co::setTcpConnectTimeout(socketfd, 300);

    uint32_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    GTimer gt;
    int n = connect(socketfd, (sockaddr*)&addr, sizeof(addr));
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, ETIMEDOUT);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    TIMER_CHECK(gt, 300, 50);

    close(socketfd);
    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GT(socketfd, 0);

    // set nonblock
    int flags = fcntl(socketfd, F_GETFL);
    int res = fcntl(socketfd, F_SETFL, flags | O_NONBLOCK);
    EXPECT_EQ(res, 0);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    gt.reset();
    n = connect(socketfd, (sockaddr*)&addr, sizeof(addr));
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EINPROGRESS);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count);
    TIMER_CHECK(gt, 0, 30);
}

TEST(IOTimed, Main)
{
//    g_Scheduler.GetOptions().debug = dbg_hook;
    go timed;
    WaitUntilNoTask();

    go connect_timeo;
    WaitUntilNoTask();
}

