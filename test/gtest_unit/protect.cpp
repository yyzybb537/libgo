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
    co_opt.debug = co::dbg_task;
    co_opt.protect_stack_page = 1;
    co_opt.stack_size = 8192;

    printf("pagesize:%d\n", getpagesize());
//    printf("minimum size: %d\n", (int)boost::context::stack_traits::minimum_size());

    co_opt.protect_stack_page = 0;
    co_opt.stack_size = 4096 * 10;
    go []{
        hold_stack<4096>();
        hold_stack<4096 + 2048>();
        hold_stack<4096 * 2>();
    };
    printf("create task ok, will run it.\n");
    WaitUntilNoTask();
    printf("run it done.\n");
}
