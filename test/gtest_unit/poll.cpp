#include "test_server.h"
#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <chrono>
#include "coroutine.h"
using namespace std;
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

void CreatePollfds(pollfd* fds, int num)
{
    for (int i = 0; i < num; ++i) {
        int socketfd = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_FALSE(-1 == socketfd);
        fds[i].fd = socketfd;
        fds[i].events = POLLIN | POLLOUT | POLLERR;
        fds[i].revents = 0;
    }
}

template <int N>
void CreatePollfds(pollfd (&fds)[N])
{
    CreatePollfds(fds, N);
}

void FreePollfds(pollfd* fds, int num)
{
    for (int i = 0; i < num; ++i) {
        close(fds[i].fd);
        fds[i].fd = -1;
    }
}

template <int N>
void FreePollfds(pollfd (&fds)[N])
{
    FreePollfds(fds, N);
}


void connect_me(int fd)
{
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(43222);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int r = connect(fd, (sockaddr*)&addr, sizeof(addr));
    EXPECT_EQ(r, 0);

//    int flags = fcntl(fd, F_GETFL);
//    flags |= O_NONBLOCK;
//    int n = fcntl(fd, F_SETFL, flags);
//    EXPECT_FALSE(n == -1);
//
//    char buf[1024];
//    n = read(fd, buf, sizeof(buf));
//    EXPECT_EQ(errno, EAGAIN);
//    EXPECT_EQ(n, -1);
//
//    n = fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
//    EXPECT_FALSE(n == -1);
}

TEST(Poll, TimeoutIs0)
{
//    g_Scheduler.GetOptions().debug = dbg_all;
    go [] {
        pollfd fds[2];
        CreatePollfds(fds);
        int n = poll(fds, 2, 0);
        EXPECT_EQ(n, 2);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0);
        FreePollfds(fds, 1);
        n = poll(fds, 2, 0);
        EXPECT_EQ(n, 1);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0);
        FreePollfds(fds);
        n = poll(fds, 2, 0);
        EXPECT_EQ(n, 0);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
}

TEST(Poll, TimeoutIsF1)
{
    go [] {
        pollfd fds[2];
        CreatePollfds(fds);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0);
        int n = poll(fds, 2, -1);
        EXPECT_EQ(n, 2);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 1);
        FreePollfds(fds, 1);
        n = poll(fds, 2, -1);
        EXPECT_EQ(n, 1);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 2);
        FreePollfds(fds);
        n = poll(fds, 2, -1);
        EXPECT_EQ(n, 0);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 3);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
}

TEST(Poll, TimeoutIs1)
{
    go [] {
        pollfd fds[2];
        CreatePollfds(fds);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0);
        int n = poll(fds, 2, 1000);
        EXPECT_EQ(n, 2);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 1);
        FreePollfds(fds, 1);
        n = poll(fds, 2, 1000);
        EXPECT_EQ(n, 1);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 2);
        FreePollfds(fds);
        n = poll(fds, 2, 1000);
        EXPECT_EQ(n, 0);
        // two times switch: io_wait and sleep.
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 4);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);

    go [] {
        pollfd fds[1];
        CreatePollfds(fds);
        connect_me(fds[0].fd);
        uint64_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
        fds[0].events = POLLIN;
        auto start = std::chrono::high_resolution_clock::now();
        int n = poll(fds, 1, 1000);
        auto end = std::chrono::high_resolution_clock::now();
        auto c = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        EXPECT_EQ(fds[0].revents, 0);
        EXPECT_LT(c, 1050);
        EXPECT_GT(c, 950);
        EXPECT_EQ(n, 0);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
        FreePollfds(fds);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
}

TEST(Poll, MultiThreads)
{
    for (int i = 0; i < 100; ++i)
        go [] {
            pollfd fds[1];
            CreatePollfds(fds);
            connect_me(fds[0].fd);
            uint64_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
            fds[0].events = POLLIN;
            auto start = std::chrono::high_resolution_clock::now();
            int n = poll(fds, 1, 1000);
            auto end = std::chrono::high_resolution_clock::now();
            auto c = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            EXPECT_LT(c, 1200);
            EXPECT_GT(c, 950);
//            EXPECT_LT(c, 1010);
//            EXPECT_GT(c, 1000);
            EXPECT_EQ(n, 0);
            EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
        };
    printf("coroutines create done.\n");
    boost::thread_group tg;
    for (int i = 0; i < 8; ++i)
        tg.create_thread([] {
                g_Scheduler.RunUntilNoTask();
                });
    tg.join_all();
    printf("coroutines run done.\n");
    EXPECT_EQ(Task::GetTaskCount(), Task::GetDeletedTaskCount());
    if (Task::GetTaskCount()) // 可能会有一些Task还未删除，执行删除逻辑。
        g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
    EXPECT_EQ(Task::GetDeletedTaskCount(), 0);
}
