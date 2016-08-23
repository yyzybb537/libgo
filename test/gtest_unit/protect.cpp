#include <iostream>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include "gtest_exit.h"
using namespace std;
using namespace co;

template <int bytes>
void hold_stack()
{
    char buf[bytes] = {};
    memset(buf, 0, sizeof(buf));
}

TEST(testProtect, testProtect)
{
    co_sched.GetOptions().debug = co::dbg_task;
    co_sched.GetOptions().protect_stack_page = 1;
    co_sched.GetOptions().stack_size = 8192;

    printf("pagesize:%d\n", getpagesize());
//    printf("minimum size: %d\n", (int)boost::context::stack_traits::minimum_size());

    bool check = false;
    co_sched.GetOptions().stack_size = 8192;
    try {
        go []{};
    } catch (std::exception& e) {
        printf("stacksize:%d, catch ex:%s\n", co_sched.GetOptions().stack_size, e.what());
        check = true;
    }
    EXPECT_TRUE(check);
    check = false;

    co_sched.GetOptions().stack_size = 4096 * 3;
    try {
        go []{};
        check = true;
        printf("stacksize:%d protect success.\n", co_sched.GetOptions().stack_size);
    } catch (std::exception& e) {
        printf("stacksize:%d, catch ex:%s\n", co_sched.GetOptions().stack_size, e.what());
    }
    EXPECT_TRUE(check);
    check = true;

    co_sched.RunUntilNoTask();

    co_sched.GetOptions().protect_stack_page = 0;
    co_sched.GetOptions().stack_size = 4096 * 10;
    go []{
        hold_stack<4096>();
        hold_stack<4096 + 2048>();
        hold_stack<4096 * 2>();
    };
    printf("create task ok, will run it.\n");
    co_sched.RunUntilNoTask();
    printf("run it done.\n");

    pid_t pid = fork();
    EXPECT_TRUE(pid >= 0);
    if (pid == -1) {
        perror("fork error:");
    } else if (!pid) {
        printf("fork child pid=%d\n", getpid());
        co_sched.GetOptions().protect_stack_page = 8;
        co_sched.GetOptions().stack_size = 4096 * 10;
        go []{
            printf("yiled 1\n");
            hold_stack<4096>();
            printf("yiled 2\n");
            hold_stack<4096 + 2048>();
            printf("yiled 3\n");
            hold_stack<4096 * 2>();
            printf("yiled 4\n");
        };
        printf("create task ok, will run it.\n");
        co_sched.RunUntilNoTask();
        printf("run it done.\n");
    } else {
        // main process, check child returns.
        int stat_val;
        pid_t child = wait(&stat_val);
        printf("child is finished. pid=%d\n", child);
        if (WIFEXITED(stat_val)) {
            printf("child exit code: %d\n", WEXITSTATUS(stat_val));;
        } else {
            printf("child terminated: %d %d\n", WIFSIGNALED(stat_val), WTERMSIG(stat_val));
        }
        EXPECT_FALSE(WIFEXITED(stat_val));
        EXPECT_TRUE(WIFSIGNALED(stat_val));
        EXPECT_EQ(WTERMSIG(stat_val), SIGSEGV);
    }
}
