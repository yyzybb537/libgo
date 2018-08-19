#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <chrono>
#include <boost/thread.hpp>
#include <sys/socket.h>
#include "coroutine.h"
#include "../gtest_exit.h"
#include "hook.h"
using namespace std;
using namespace std::chrono;
using namespace co;

///poll test points:
// 1.timeout == 0
// 2.timeout == -1
// 3.timeout == 1
// 4.all of fds are valid.
// 5.all of fds are invalid.
// 6.some -1 in fds.
//X7.some file_fd in fds.
//X8.occurred ERR events
// 9.timeout return.
// 10.multi threads.

void timeoutIs0()
{
    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);
    HOOK_EQ(res);

    auto yc = g_Scheduler.GetCurrentTaskYieldCount();

    {
        pollfd pfds[1] = {{fds[0], POLLIN, 0}};
        int n = poll(pfds, 1, 0);
        EXPECT_EQ(n, 0);
        HOOK_EQ(pfds[0].revents);
    }

    {
        pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
        int n = poll(pfds, 1, 0);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 0);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 2);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    shutdown(fds[0], SHUT_RDWR);

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 2);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    EXPECT_EQ(close(fds[0]), 0);
    usleep(10 * 1000);
    yc++;

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 2);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    if (Processer::IsCoroutine()) {
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc);
    }

    fds[0] = -1;

    {
        pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
        int n = poll(pfds, 1, 0);
        EXPECT_EQ(n, 0);
        HOOK_EQ(pfds[0].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    if (Processer::IsCoroutine()) {
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc);
    }
    EXPECT_EQ(close(fds[1]), 0);
}

TEST(Poll, TimeoutIs0)
{
//    co_opt.debug = dbg_ioblock | dbg_fd_ctx;
    timeoutIs0();

    go [] {
        timeoutIs0();
    };
    WaitUntilNoTask();
//    co_opt.debug = 0;
}

void timeoutIsNegative1()
{
    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    // poll无限时等待数据到达
    {
        go [=]{
            sleep(1);
            write(fds[1], "a", 1);
        };
        GTimer gt;
        pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
        int n = poll(pfds, 2, -1);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
        TIMER_CHECK(gt, 1000, 50);

        char buf;
        EXPECT_EQ(read(fds[0], &buf, 1), 1);
        EXPECT_EQ(buf, 'a');
    }

    {
        pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
        int n = poll(pfds, 1, -1);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, -1);
        EXPECT_EQ(n, 2);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, -1);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN, 0}};
        int n = poll(pfds, 2, -1);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    shutdown(fds[0], SHUT_RDWR);

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, -1);
        EXPECT_EQ(n, 2);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    close(fds[0]);
    usleep(10 * 1000);

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
        int n = poll(pfds, 2, 0);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    fds[0] = -1;

    {
        // poll invalid fds with negative timeout
        pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
        auto yc = g_Scheduler.GetCurrentTaskYieldCount();
        int n = poll(pfds, 1, 0);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc);
        EXPECT_EQ(n, 0);
        HOOK_EQ(pfds[0].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
        int n = poll(pfds, 2, 0);
        EXPECT_EQ(n, 1);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }
}

TEST(Poll, TimeoutIsNegative1)
{
//    ::co::CoroutineOptions::getInstance().debug = dbg_suspend;
    timeoutIsNegative1();

    go [] {
        timeoutIsNegative1();
    };
    WaitUntilNoTask();
}

void timeoutIs1()
{
    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    // poll无限时等待数据到达
    {
        go [=]{
            //                GTimer gt;
            co_sleep(200);
            //                printf("co_sleep(200) cost %d ms\n", gt.ms());
            write(fds[1], "a", 1);
        };
        GTimer gt;
        pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
        //            GTimer gtx;
        int n = poll(pfds, 2, 2000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
        //            printf("tk(%s) poll cost %d ms\n", co::Processer::GetCurrentTask()->DebugInfo(), gtx.ms());
        //            co_sleep(50);
        //            exit(1);
        TIMER_CHECK(gt, 200, 50);

        char buf;
        gt.reset();
        EXPECT_EQ(read(fds[0], &buf, 1), 1);
        EXPECT_LT(gt.ms(), 20);
        EXPECT_EQ(buf, 'a');
    }

    {
        go [=]{
            co_sleep(500);
            ssize_t n = write(fds[1], "a", 1);
            (void)n;
        };
        GTimer gt;
        pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
        int n = poll(pfds, 2, 200);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
        TIMER_CHECK(gt, 200, 50);

        gt.reset();
        n = poll(pfds, 2, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
        TIMER_CHECK(gt, 250, 100);

        char buf;
        EXPECT_EQ(read(fds[0], &buf, 1), 1);
        EXPECT_EQ(buf, 'a');
    }

    {
        pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
        int n = poll(pfds, 1, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN, 0}};
        int n = poll(pfds, 2, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    shutdown(fds[0], SHUT_RDWR);

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
        int n = poll(pfds, 2, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    close(fds[0]);
    usleep(10 * 1000);

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
        int n = poll(pfds, 2, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }

    fds[0] = -1;

    {
        pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
        GTimer gt;
        auto yc = g_Scheduler.GetCurrentTaskYieldCount();
        int n = poll(pfds, 1, 300);
        if (Processer::IsCoroutine()) {
            EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc + 1);
        }
        TIMER_CHECK(gt, 300, 50);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
    }

    {
        pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
        int n = poll(pfds, 2, 1000);
        HOOK_EQ(n);
        HOOK_EQ(pfds[0].revents);
        HOOK_EQ(pfds[1].revents);
    }
}

TEST(Poll, TimeoutIs1)
{
//    ::co::CoroutineOptions::getInstance().debug = dbg_ioblock | dbg_fd_ctx | dbg_suspend | dbg_yield | dbg_switch;
    timeoutIs1();

    go [] {
        timeoutIs1();
    };
    WaitUntilNoTask();
}

TEST(PollTrigger, MultiPollTimeout1)
{
//    ::co::CoroutineOptions::getInstance().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    res = fill_send_buffer(fds[0]);
    cout << "fill " << res << " bytes." << endl;

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 1000);
            TIMER_CHECK(gt, 1000, 50);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
        };
    }

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 500);
            TIMER_CHECK(gt, 500, 50);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
        };
    }
    WaitUntilNoTask();

//    ::co::CoroutineOptions::getInstance().debug = 0;
    close(fds[0]);
    close(fds[1]);
//    g_Scheduler.UseAloneTimerThread();
}

TEST(PollTrigger, MultiPollTimeout2)
{
//    ::co::CoroutineOptions::getInstance().debug = dbg_ioblock | dbg_fd_ctx | dbg_suspend;

    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
            GTimer gt;
            int n = poll(pfds, 2, 1000);
            TIMER_CHECK(gt, 1000, 50);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, 0);
        };
    }

    WaitUntilNoTask();
    close(fds[0]);
    close(fds[1]);

    res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    int fill1 = fill_send_buffer(fds[0]);
    int fill2 = fill_send_buffer(fds[1]);

    cout << "fill " << fill1 << " bytes." << endl;
    cout << "fill " << fill2 << " bytes." << endl;

    DebugPrint(dbg_all, "fill done!!!!!!!!!!!!!!!!!!!!!!!!!!!");

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            GTimer gt;
            int n = poll(pfds, 2, 500);
            TIMER_CHECK(gt, 500, 50);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, 0);
        };
    }
    WaitUntilNoTask();

    ::co::CoroutineOptions::getInstance().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(PollTrigger, MultiPollTrigger)
{
//    ::co::CoroutineOptions::getInstance().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 1000);
            TIMER_CHECK(gt, 500, 50);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLIN);
        };
    }

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
            EXPECT_EQ(pfds[1].revents, POLLOUT);
        };
    }

    go [=] {
        co_sleep(500);
        ssize_t n = write(fds[1], "a", 1);
        (void)n;
    };
    WaitUntilNoTask();

//    ::co::CoroutineOptions::getInstance().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(PollTrigger, MultiPollClose)
{
//    ::co::CoroutineOptions::getInstance().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    fill_send_buffer(fds[0]);

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 1000);
            TIMER_CHECK(gt, 500, 50);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLNVAL);
            cout << "read wait exit" << endl;
        };
    }

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 1000);
            TIMER_CHECK(gt, 500, 50);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLNVAL);
            cout << "write wait exit" << endl;
        };
    }

    go [=] {
        co_sleep(500);
        cout << "close " << fds[0] << endl;
        close(fds[0]);
    };
    WaitUntilNoTask();

//    ::co::CoroutineOptions::getInstance().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(PollTrigger, MultiPollShutdown)
{
//    ::co::CoroutineOptions::getInstance().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    fill_send_buffer(fds[0]);

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 1000);
            TIMER_CHECK(gt, 500, 50);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLIN|POLLHUP);
            cout << "read wait exit" << endl;
        };
    }

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 1000);
            TIMER_CHECK(gt, 500, 50);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLOUT|POLLHUP);
            cout << "write wait exit" << endl;
        };
    }

    go [=] {
        co_sleep(500);
        cout << "shutdown " << fds[0] << endl;
        shutdown(fds[0], SHUT_RDWR);
    };
    WaitUntilNoTask();

//    ::co::CoroutineOptions::getInstance().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(Poll, MultiThreads)
{
    for (int i = 0; i < 20; ++i)
        go [] {
            int fds[2];
            int res = tcpSocketPair(AF_LOCAL, SOCK_STREAM, 0, fds);
            EXPECT_EQ(res, 0);
            uint64_t yc = g_Scheduler.GetCurrentTaskYieldCount();
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            GTimer gt;
            int n = poll(pfds, 1, 1000);
            TIMER_CHECK(gt, 1000, 200);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc + 1);
            EXPECT_EQ(close(fds[0]), 0);
            EXPECT_EQ(close(fds[1]), 0);
            close(fds[0]);
            close(fds[1]);
        };
    WaitUntilNoTask();
}
