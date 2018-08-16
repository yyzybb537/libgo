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
    ++c;
    co_yield;
    ++c;
    co_yield;
}

TEST_P(MassCo, LittleStack)
{
    size_t n = n_;
    for (size_t i = 0; i < n; ++i)
        go co_stack(8192) foo;

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    WaitUntilNoTask();
}

TEST_P(MassCo, CnK)
{
    size_t n = n_;
    for (size_t i = 0; i < n; ++i)
        go foo;

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }

    WaitUntilNoTask();

    if (is_show_memory)
    {
        pinfo pi;
        cout << n << " coroutines, VirtMem: " << pi.get_virt_str() << ", RealMem: " << pi.get_mem_str() << endl;
    }
}

INSTANTIATE_TEST_CASE_P(
	MassCoTest,
	MassCo,
	Values(100, 1000, 10000));
