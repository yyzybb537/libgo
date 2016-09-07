#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include <fstream>
#include <time.h>
#include <stdlib.h>
#include "gtest_exit.h"
#include "pinfo.h"
using namespace std;
using namespace co;

using ::testing::TestWithParam;
using ::testing::Values;

bool is_show_memory = true;

struct MassCo : public TestWithParam<int>
{
    int n_;
    void SetUp() { n_ = GetParam(); }
};

static uint32_t c = 0;
void foo()
{
//    int buf[1024] = {};
//    memset(buf, 1, sizeof(buf));
//    buf[0] = 1;

    ++c;
    co_yield;
    ++c;
    co_yield;
}

TEST_P(MassCo, LittleStack)
{
//    co_sched.GetOptions().debug = dbg_task;

    c = 0;
    size_t n = n_;
    for (size_t i = 0; i < n; ++i)
        go_stack(8192) foo;

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    co_sched.Run();
    EXPECT_EQ(c, n);

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    co_sched.Run();
    EXPECT_EQ(c, n * 2);

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    co_sched.RunUntilNoTask();
    EXPECT_TRUE(co_sched.IsEmpty());

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    co_sched.RunUntilNoTask();
    EXPECT_EQ(co::Task::GetTaskCount(), 0u);
}

TEST_P(MassCo, CnK)
{
    c = 0;
//    if (n_ == 1)
//        co_sched.GetOptions().debug = dbg_scheduler;

//    void* store[1024];
//    for (int i = 0; i < 1024; ++i)
//        store[i] = (char*)malloc(300 * 1024 * 1024);

    size_t n = n_;
    for (size_t i = 0; i < n; ++i)
        go foo;

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    co_sched.Run();
    EXPECT_EQ(c, n);

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    co_sched.Run();
    EXPECT_EQ(c, n * 2);

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    co_sched.RunUntilNoTask();
    EXPECT_TRUE(co_sched.IsEmpty());

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

//    printf("press anykey to continue. task_c=%u\n", (uint32_t)co::Task::GetTaskCount());
//    getchar();
//    co_sched.GetOptions().debug = dbg_none;

    co_sched.RunUntilNoTask();
    EXPECT_EQ(co::Task::GetTaskCount(), 0u);
//    printf("press anykey to continue. task_c=%u\n", (uint32_t)co::Task::GetTaskCount());
//    getchar();

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

//    for (int i = 0; i < 1024; ++i)
//        free(store[i]);
}

#ifdef SMALL_TEST
INSTANTIATE_TEST_CASE_P(
	MassCoTest,
	MassCo,
	Values(100, 1000, 10000));
#else
INSTANTIATE_TEST_CASE_P(
	MassCoTest,
	MassCo,
	Values(100, 1000, 10000, 100000));
#endif
