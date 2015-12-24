#include "test_server.h"
#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include <sys/select.h>
#include <time.h>
#include <chrono>
#include <boost/any.hpp>
#include "coroutine.h"
using namespace std;
using namespace co;

///select test points:
// 1.timeout == 0 seconds (immedaitely)
// 2.timeout == NULL
// 3.timeout == 1 seconds
// 4.all of fds are valid
// 5.all of fds are invalid
// 6.some -1 in fds
//X7.some file_fd in fds
//X8.occurred ERR events
// 9.timeout return
// 10.multi threads

typedef int(*select_t)(int nfds, fd_set *readfds, fd_set *writefds,
                          fd_set *exceptfds, struct timeval *timeout);

bool FD_EQUAL(fd_set *lhs, fd_set *rhs){
    return memcmp(lhs, rhs, sizeof(fd_set)) == 0;
}

bool FD_ISZERO(fd_set *fds){
    for (int i = 0; i < FD_SETSIZE; ++i)
        if (FD_ISSET(i, fds))
            return false;
    return true;
}

int FD_SIZE(fd_set *fds){
    int n = 0;
    for (int i = 0; i < FD_SETSIZE; ++i)
        if (FD_ISSET(i, fds))
            ++n;
    return n;
}

struct GcNew
{
    std::unique_ptr<boost::any> holder_;

    template <typename T>
    T* operator-(T* ptr) {
        holder_.reset(new boost::any(std::shared_ptr<T>(ptr)));
        return ptr;
    }
};
#define gc_new GcNew()-new

struct AutoFreeFdSet
{
    fd_set fds_;
    int nfds_;
    AutoFreeFdSet(fd_set* fds) : fds_(*fds), nfds_(0) {
        for (int i = 0; i < FD_SETSIZE; ++i)
            if (FD_ISSET(i, &fds_))
                nfds_ = i + 1;
    }
    ~AutoFreeFdSet() {
        for (int i = 0; i < FD_SETSIZE; ++i)
            if (FD_ISSET(i, &fds_))
                close(i);
    }

    bool operator==(fd_set *fds) {
        return FD_EQUAL(&fds_, fds);
    }

    bool operator==(fd_set & fds) {
        return FD_EQUAL(&fds_, &fds);
    }
};

void connect_me(int fd)
{
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(43222);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int r = connect(fd, (sockaddr*)&addr, sizeof(addr));
    EXPECT_EQ(r, 0);
}

std::shared_ptr<AutoFreeFdSet> CreateFds(fd_set* fds, int num)
{
    FD_ZERO(fds);
    for (int i = 0; i < num; ++i) {
        int socketfd = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_FALSE(-1 == socketfd);
        EXPECT_LT(socketfd, FD_SETSIZE);
        connect_me(socketfd);
        FD_SET(socketfd, fds);
    }

    return std::shared_ptr<AutoFreeFdSet>(new AutoFreeFdSet(fds));
}

static timeval zero_timeout = {0, 0};

TEST(Select, TimeoutIs0)
{
//    g_Scheduler.GetOptions().debug = dbg_all;
//    g_Scheduler.GetOptions().debug_output = fopen("log", "w+");
    go [] {
        fd_set wr_fds;
        auto x = CreateFds(&wr_fds, 2);
        uint64_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
        EXPECT_EQ(FD_SIZE(&wr_fds), 2);
        EXPECT_FALSE(FD_ISZERO(&wr_fds));
        int n = select(x->nfds_, NULL, &wr_fds, NULL, &zero_timeout);
        EXPECT_EQ(n, 2);
        EXPECT_TRUE(*x == wr_fds);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
}

TEST(Select, TimeoutIsF1)
{
    go [] {
        fd_set wr_fds;
        auto x = CreateFds(&wr_fds, 2);
        EXPECT_EQ(FD_SIZE(&wr_fds), 2);
        uint64_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
        int n = select(x->nfds_, NULL, &wr_fds, NULL, NULL);
        EXPECT_TRUE(*x == wr_fds);
        EXPECT_EQ(n, 2);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);

        fd_set rd_fds;
        auto r = CreateFds(&rd_fds, 2);
        EXPECT_EQ(FD_SIZE(&rd_fds), 2);
        yield_count = g_Scheduler.GetCurrentTaskYieldCount();
        n = select(std::max(x->nfds_, r->nfds_), &rd_fds, &wr_fds, NULL, NULL);
        EXPECT_TRUE(*x == wr_fds);
        EXPECT_TRUE(FD_ISZERO(&rd_fds));
        EXPECT_EQ(n, 2);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
}

TEST(Select, TimeoutIs1)
{
    go [] {
        fd_set wr_fds;
        auto x = CreateFds(&wr_fds, 2);
        EXPECT_EQ(FD_SIZE(&wr_fds), 2);
        uint64_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
        int n = select(x->nfds_, NULL, &wr_fds, NULL, gc_new timeval{1, 0});
        EXPECT_TRUE(*x == wr_fds);
        EXPECT_EQ(n, 2);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);

        fd_set rd_fds;
        auto r = CreateFds(&rd_fds, 2);
        EXPECT_EQ(FD_SIZE(&rd_fds), 2);
        yield_count = g_Scheduler.GetCurrentTaskYieldCount();
        n = select(std::max(x->nfds_, r->nfds_), &rd_fds, &wr_fds, NULL, gc_new timeval{1, 0});
        EXPECT_TRUE(*x == wr_fds);
        EXPECT_TRUE(FD_ISZERO(&rd_fds));
        EXPECT_EQ(n, 2);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);

    go [] {
        fd_set rd_fds;
        auto r = CreateFds(&rd_fds, 2);
        EXPECT_EQ(FD_SIZE(&rd_fds), 2);
        uint64_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
        auto start = std::chrono::high_resolution_clock::now();
        int n = select(r->nfds_, &rd_fds, NULL, NULL, gc_new timeval{1, 0});
        auto end = std::chrono::high_resolution_clock::now();
        auto c = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        EXPECT_FALSE(*r == rd_fds);
        EXPECT_TRUE(FD_ISZERO(&rd_fds));
        EXPECT_EQ(n, 0);
        EXPECT_LT(c, 1050);
        EXPECT_GT(c, 950);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
}

TEST(Select, Sleep)
{
    go [] {
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 0);
        auto start = std::chrono::high_resolution_clock::now();
        int n = select(0, NULL, NULL, NULL, gc_new timeval{1, 0});
        auto end = std::chrono::high_resolution_clock::now();
        auto c = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        EXPECT_EQ(n, 0);
        EXPECT_LT(c, 1050);
        EXPECT_GT(c, 950);
        EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), 1);
    };
    g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
}

TEST(Select, MultiThreads)
{
//    co_sched.GetOptions().debug = co::dbg_hook;
    for (int i = 0; i < 50; ++i)
        go [] {
            fd_set rd_fds;
            auto r = CreateFds(&rd_fds, 2);
            EXPECT_EQ(FD_SIZE(&rd_fds), 2);
            uint64_t yield_count = g_Scheduler.GetCurrentTaskYieldCount();
            auto start = std::chrono::high_resolution_clock::now();
            int n = select(r->nfds_, &rd_fds, NULL, NULL, gc_new timeval{1, 0});
            auto end = std::chrono::high_resolution_clock::now();
            auto c = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            EXPECT_FALSE(*r == rd_fds);
            EXPECT_TRUE(FD_ISZERO(&rd_fds));
            EXPECT_EQ(n, 0);
            EXPECT_LT(c, 1100);
            EXPECT_GT(c, 999);
            EXPECT_EQ(g_Scheduler.GetCurrentTaskYieldCount(), yield_count + 1);
        };
    boost::thread_group tg;
    for (int i = 0; i < 8; ++i)
        tg.create_thread([] {
                g_Scheduler.RunUntilNoTask();
                });
    tg.join_all();
    EXPECT_EQ(Task::GetTaskCount(), Task::GetDeletedTaskCount());
    if (Task::GetTaskCount()) // 可能会有一些Task还未删除，执行删除逻辑。
        g_Scheduler.RunUntilNoTask();
    EXPECT_EQ(Task::GetTaskCount(), 0);
    EXPECT_EQ(Task::GetDeletedTaskCount(), 0);
}
