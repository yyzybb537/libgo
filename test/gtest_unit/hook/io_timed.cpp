#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include "coroutine.h"

#if defined(LIBGO_SYS_Unix)
# include "netio/unix/hook.h"
# include <sys/fcntl.h>
# include <sys/types.h>
#elif defined(LIBGO_SYS_Windows)
# include <WinSock2.h>
# include <Windows.h>
#endif

#include "../gtest_exit.h"
#include "hook.h"
using namespace std;
using namespace co;

void setNonblocking(int fd) {
#if defined(LIBGO_SYS_Unix)
    int flags = fcntl(fd, F_GETFL);
    int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    EXPECT_EQ(res, 0);
#elif defined(LIBGO_SYS_Windows)
    u_long arg = 1;
    int res = ioctlsocket((SOCKET)fd, FIONBIO, &arg);
    EXPECT_EQ(res, 0);
#endif
}

void setTimeo(int socketfd, int type, int ms) {
#if defined(LIBGO_SYS_Unix)
    timeval rcvtimeout = { ms / 1000, (ms % 1000) * 1000 };
    int res = setsockopt(socketfd, SOL_SOCKET, type, (const char*)&rcvtimeout, sizeof(rcvtimeout));
    ASSERT_EQ(res, 0);
#elif defined(LIBGO_SYS_Windows)
    int res = setsockopt(socketfd, SOL_SOCKET, type, (const char*)&ms, sizeof(ms));
    ASSERT_EQ(res, 0);
#endif
}

#if defined(LIBGO_SYS_Unix)
# define checkEAGAIN() EXPECT_EQ(errno, EAGAIN)
# define checkTimeout() EXPECT_EQ(errno, EAGAIN)
#elif defined(LIBGO_SYS_Windows)
# define checkEAGAIN() EXPECT_EQ(WSAGetLastError(), WSAEWOULDBLOCK)
# define checkTimeout() EXPECT_EQ(WSAGetLastError(), WSAETIMEDOUT)
#endif

void timed()
{
    co_opt.debug = co::dbg_hook;

    int fds[2];
    int res = tcpSocketPair(0, SOCK_STREAM, 0, fds);
    ASSERT_EQ(res, 0);

    int socketfd = fds[0];

    setTimeo(socketfd, SO_RCVTIMEO, 1100);
    setTimeo(socketfd, SO_SNDTIMEO, 500);

    res = fill_send_buffer(socketfd);
    cout << "fill " << res << " bytes on write buffer." << endl;

    char buf[1024] = {};
    uint32_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    GTimer gt;
retry:
    gt.reset();
    ssize_t n = ::send(socketfd, buf, sizeof(buf), 0);
    if (n > 0) {
        res += n;
        goto retry;
    }
    cout << "fill " << res << " bytes on write buffer." << endl;
    EXPECT_EQ(n, -1);
    checkEAGAIN();
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    TIMER_CHECK(gt, 500, 50);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    gt.reset();
    n = ::recv(socketfd, buf, sizeof(buf), 0);
    EXPECT_EQ(n, -1);
    checkEAGAIN();
    //EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    TIMER_CHECK(gt, 1100, 50);

    // set nonblock
    setNonblocking(socketfd);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    gt.reset();
    n = ::recv(socketfd, buf, sizeof(buf), 0);
    EXPECT_EQ(n, -1);
    checkEAGAIN();
    //EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count);
    TIMER_CHECK(gt, 0, 30);
}

#if defined(LIBGO_SYS_Unix)
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
    setNonblocking(socketfd);

    yield_count = g_Scheduler.GetCurrentTaskYieldCount();
    gt.reset();
    n = connect(socketfd, (sockaddr*)&addr, sizeof(addr));
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EINPROGRESS);
    EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count);
    TIMER_CHECK(gt, 0, 30);
}
#endif

TEST(IOTimed, Main)
{
//    g_Scheduler.GetOptions().debug = dbg_hook;
    go timed;
    WaitUntilNoTask();

#if defined(LIBGO_SYS_Unix)
    go connect_timeo;
    WaitUntilNoTask();
#endif
}

