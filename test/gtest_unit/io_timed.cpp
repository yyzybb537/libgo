#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include "linux/linux_glibc_hook.h"
#include <sys/fcntl.h>
#include <sys/types.h>
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
    auto start = std::chrono::high_resolution_clock::now();
retry:
    start = std::chrono::high_resolution_clock::now();
    ssize_t n = write(socketfd, buf, sizeof(buf));
    if (n > 0) {
        res += n;
        goto retry;
    }
    cout << "fill " << res << " bytes on write buffer." << endl;
    auto end = std::chrono::high_resolution_clock::now();
    auto milli = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    EXPECT_LT(milli, 550);
    EXPECT_GT(milli, 499);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    start = std::chrono::high_resolution_clock::now();
    n = read(socketfd, buf, sizeof(buf));
    end = std::chrono::high_resolution_clock::now();
    milli = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    EXPECT_LT(milli, 1150);
    EXPECT_GT(milli, 1099);

    // set nonblock
    int flags = fcntl(socketfd, F_GETFL);
    res = fcntl(socketfd, F_SETFL, flags | O_NONBLOCK);
    EXPECT_EQ(res, 0);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    start = std::chrono::high_resolution_clock::now();
    n = read(socketfd, buf, sizeof(buf));
    end = std::chrono::high_resolution_clock::now();
    milli = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count);
    EXPECT_LT(milli, 10);
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
    co::set_connect_timeout(300);

    uint32_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    auto start = std::chrono::high_resolution_clock::now();
    int n = connect(socketfd, (sockaddr*)&addr, sizeof(addr));
    auto end = std::chrono::high_resolution_clock::now();
    auto milli = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, ETIMEDOUT);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    EXPECT_LT(milli, 350);
    EXPECT_GT(milli, 299);

    close(socketfd);
    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GT(socketfd, 0);

    // set nonblock
    int flags = fcntl(socketfd, F_GETFL);
    int res = fcntl(socketfd, F_SETFL, flags | O_NONBLOCK);
    EXPECT_EQ(res, 0);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    start = std::chrono::high_resolution_clock::now();
    n = connect(socketfd, (sockaddr*)&addr, sizeof(addr));
    end = std::chrono::high_resolution_clock::now();
    milli = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EINPROGRESS);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count);
    EXPECT_LT(milli, 10);
}

TEST(IOTimed, Main)
{
//    g_Scheduler.GetOptions().debug = dbg_hook;
    go timed;
    g_Scheduler.RunUntilNoTask();

    go connect_timeo;
    g_Scheduler.RunUntilNoTask();
}

