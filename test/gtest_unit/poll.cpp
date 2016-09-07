#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <chrono>
#include <boost/thread.hpp>
#include "coroutine.h"
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

static int fill_send_buffer(int fd)
{
    static char* buf = new char[10240];
    int c = 0;
    for (;;) {
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 0) <= 0)
            break;
        c += write(fd, buf, 10240);
    }
    return c;
}

TEST(Poll, TimeoutIs0)
{
    go [] {
        int fds[2];
        int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
        EXPECT_EQ(res, 0);

        {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            int n = poll(pfds, 1, 0);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
        }

        {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            int n = poll(pfds, 1, 0);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
            int n = poll(pfds, 2, 0);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, 0);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, 0);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
            EXPECT_EQ(pfds[1].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, 0);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN, 0}};
            int n = poll(pfds, 2, 0);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
            EXPECT_EQ(pfds[1].revents, 0);
        }

        shutdown(fds[0], SHUT_RDWR);

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, 0);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLOUT|POLLHUP);
            EXPECT_EQ(pfds[1].revents, POLLOUT|POLLHUP);
        }

        EXPECT_EQ(close(fds[0]), 0);

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
            int n = poll(pfds, 2, 0);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLNVAL);
            EXPECT_EQ(pfds[1].revents, POLLIN|POLLOUT|POLLHUP);
        }

        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0u);

        fds[0] = -1;

        {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            int n = poll(pfds, 1, 0);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
            int n = poll(pfds, 2, 0);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, POLLIN|POLLOUT|POLLHUP);
        }

        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0u);
        EXPECT_EQ(close(fds[1]), 0);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);
}

TEST(Poll, TimeoutIsNegative1)
{
//    g_Scheduler.GetOptions().debug = dbg_all;
    go [] {
        int fds[2];
        int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
        EXPECT_EQ(res, 0);

        // poll无限时等待数据到达
        {
            go [=]{
                sleep(1);
                ssize_t n = write(fds[1], "a", 1);
                (void)n;
            };
            auto start = system_clock::now();
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
            int n = poll(pfds, 2, -1);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLIN);
            EXPECT_EQ(pfds[1].revents, 0);
            auto elapse = duration_cast<milliseconds>(system_clock::now() - start).count();
            EXPECT_GT(elapse, 999);
            EXPECT_LT(elapse, 1050);
            char buf;
            EXPECT_EQ(read(fds[0], &buf, 1), 1);
            EXPECT_EQ(buf, 'a');
        }

        {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            int n = poll(pfds, 1, -1);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, -1);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
            EXPECT_EQ(pfds[1].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, -1);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN, 0}};
            int n = poll(pfds, 2, -1);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
            EXPECT_EQ(pfds[1].revents, 0);
        }

        shutdown(fds[0], SHUT_RDWR);

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, -1);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLOUT|POLLHUP);
            EXPECT_EQ(pfds[1].revents, POLLOUT|POLLHUP);
        }

        close(fds[0]);

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
            int n = poll(pfds, 2, -1);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLNVAL);
            EXPECT_EQ(pfds[1].revents, POLLIN|POLLOUT|POLLHUP);
        }

        fds[0] = -1;

        {
            // poll invalid fds with negative timeout, as co_yield.
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            auto yc = g_Scheduler.GetCurrentTaskYieldCount();
            int n = poll(pfds, 1, -1);
            EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc + 1);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
            int n = poll(pfds, 2, -1);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, POLLIN|POLLOUT|POLLHUP);
        }
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);
}

TEST(Poll, TimeoutIs1)
{
//    g_Scheduler.GetOptions().debug = dbg_all;
    go [] {
        int fds[2];
        int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
        EXPECT_EQ(res, 0);

        // poll无限时等待数据到达
        {
            go [=]{
                co_sleep(200);
                ssize_t n = write(fds[1], "a", 1);
                (void)n;
            };
            auto start = system_clock::now();
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
            int n = poll(pfds, 2, 2000);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLIN);
            EXPECT_EQ(pfds[1].revents, 0);
            auto elapse = duration_cast<milliseconds>(system_clock::now() - start).count();
            EXPECT_GT(elapse, 199);
            EXPECT_LT(elapse, 250);

            char buf;
            EXPECT_EQ(read(fds[0], &buf, 1), 1);
            EXPECT_EQ(buf, 'a');
        }

        {
            go [=]{
                co_sleep(500);
                ssize_t n = write(fds[1], "a", 1);
                (void)n;
            };
            auto start = system_clock::now();
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
            int n = poll(pfds, 2, 200);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, 0);
            auto elapse = duration_cast<milliseconds>(system_clock::now() - start).count();
            EXPECT_GT(elapse, 199);
            EXPECT_LT(elapse, 250);

            start = system_clock::now();
            n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLIN);
            EXPECT_EQ(pfds[1].revents, 0);
            elapse = duration_cast<milliseconds>(system_clock::now() - start).count();
            EXPECT_GT(elapse, 250);
            EXPECT_LT(elapse, 350);

            char buf;
            EXPECT_EQ(read(fds[0], &buf, 1), 1);
            EXPECT_EQ(buf, 'a');
        }

        {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            int n = poll(pfds, 1, 1000);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
            EXPECT_EQ(pfds[1].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, POLLOUT);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN, 0}};
            int n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLOUT);
            EXPECT_EQ(pfds[1].revents, 0);
        }

        shutdown(fds[0], SHUT_RDWR);

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            int n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLOUT|POLLHUP);
            EXPECT_EQ(pfds[1].revents, POLLOUT|POLLHUP);
        }

        close(fds[0]);

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
            int n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 2);
            EXPECT_EQ(pfds[0].revents, POLLNVAL);
            EXPECT_EQ(pfds[1].revents, POLLIN|POLLOUT|POLLHUP);
        }

        fds[0] = -1;

        {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            auto start = system_clock::now();
            auto yc = g_Scheduler.GetCurrentTaskYieldCount();
            int n = poll(pfds, 1, 300);
            EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc + 1);
            auto elapse = duration_cast<milliseconds>(system_clock::now() - start).count();
            EXPECT_GT(elapse, 299);
            EXPECT_LT(elapse, 350);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
        }

        {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLIN|POLLOUT, 0}};
            int n = poll(pfds, 2, 1000);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, POLLIN|POLLOUT|POLLHUP);
        }
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);
}

TEST(PollTrigger, MultiPollTimeout1)
{
//    g_Scheduler.GetOptions().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    res = fill_send_buffer(fds[0]);
    cout << "fill " << res << " bytes." << endl;

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            auto start = high_resolution_clock::now();
            int n = poll(pfds, 1, 1000);
            auto end = system_clock::now();
            auto c = duration_cast<milliseconds>(end - start).count();
            EXPECT_LT(c, 1050);
            EXPECT_GT(c, 999);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
        };
    }

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            auto start = high_resolution_clock::now();
            int n = poll(pfds, 1, 500);
            auto end = system_clock::now();
            auto c = duration_cast<milliseconds>(end - start).count();
            EXPECT_LT(c, 550);
            EXPECT_GT(c, 499);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, 0);
        };
    }
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);

    g_Scheduler.GetOptions().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(PollTrigger, MultiPollTimeout2)
{
//    g_Scheduler.GetOptions().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[2] = {{fds[0], POLLIN, 0}, {fds[1], POLLIN, 0}};
            auto start = high_resolution_clock::now();
            int n = poll(pfds, 2, 1000);
            auto end = system_clock::now();
            auto c = duration_cast<milliseconds>(end - start).count();
            EXPECT_LT(c, 1050);
            EXPECT_GT(c, 999);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, 0);
        };
    }

    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);

    res = fill_send_buffer(fds[0]);
    cout << "fill " << res << " bytes." << endl;
    res = fill_send_buffer(fds[1]);
    cout << "fill " << res << " bytes." << endl;

    for (int i = 0; i < 10; ++i) {
        go [=] {
            pollfd pfds[2] = {{fds[0], POLLOUT, 0}, {fds[1], POLLOUT, 0}};
            auto start = high_resolution_clock::now();
            int n = poll(pfds, 2, 500);
            auto end = system_clock::now();
            auto c = duration_cast<milliseconds>(end - start).count();
            EXPECT_LT(c, 550);
            EXPECT_GT(c, 499);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(pfds[0].revents, 0);
            EXPECT_EQ(pfds[1].revents, 0);
        };
    }
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);

    g_Scheduler.GetOptions().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(PollTrigger, MultiPollTrigger)
{
//    g_Scheduler.GetOptions().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            auto start = high_resolution_clock::now();
            int n = poll(pfds, 1, 1000);
            auto end = system_clock::now();
            auto c = duration_cast<milliseconds>(end - start).count();
            EXPECT_LT(c, 550);
            EXPECT_GT(c, 499);
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
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);

    g_Scheduler.GetOptions().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(PollTrigger, MultiPollClose)
{
//    g_Scheduler.GetOptions().debug = dbg_ioblock | dbg_fd_ctx;

    int fds[2];
    int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
    EXPECT_EQ(res, 0);

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            auto start = high_resolution_clock::now();
            int n = poll(pfds, 1, 1000);
            auto end = system_clock::now();
            auto c = duration_cast<milliseconds>(end - start).count();
            EXPECT_LT(c, 550);
            EXPECT_GT(c, 499);
            EXPECT_EQ(n, 1);
            EXPECT_EQ(pfds[0].revents, POLLNVAL);
            cout << "read wait exit" << endl;
        };
    }

    fill_send_buffer(fds[0]);

    for (int i = 0; i < 1; ++i) {
        go [=] {
            pollfd pfds[1] = {{fds[0], POLLOUT, 0}};
            int n = poll(pfds, 1, 1000);
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
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);

//    g_Scheduler.GetOptions().debug = 0;
    close(fds[0]);
    close(fds[1]);
}

TEST(Poll, MultiThreads)
{
    for (int i = 0; i < 20; ++i)
        go [] {
            int fds[2];
            int res = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
            EXPECT_EQ(res, 0);
            uint64_t yc = g_Scheduler.GetCurrentTaskYieldCount();
            pollfd pfds[1] = {{fds[0], POLLIN, 0}};
            auto start = high_resolution_clock::now();
            int n = poll(pfds, 1, 1000);
            auto end = system_clock::now();
            auto c = duration_cast<milliseconds>(end - start).count();
            EXPECT_LT(c, 1200);
            EXPECT_GT(c, 999);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yc + 1);
            EXPECT_EQ(close(fds[0]), 0);
            EXPECT_EQ(close(fds[1]), 0);
            close(fds[0]);
            close(fds[1]);
        };
    printf("coroutines create done.\n");
    boost::thread_group tg;
    for (int i = 0; i < 8; ++i)
        tg.create_thread([] {
                g_Scheduler.RunUntilNoTask();
                });
    tg.join_all();
    printf("coroutines run done.\n");
    if (Task::GetTaskCount()) // 可能会有一些Task还未删除，执行删除逻辑。
        g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0u);
}
