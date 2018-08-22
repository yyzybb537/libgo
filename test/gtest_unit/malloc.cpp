#include <iostream>
#include <gtest/gtest.h>
#include <chrono>
#include <boost/thread.hpp>
#include "gtest_exit.h"
#include "coroutine.h"
using namespace std;
using namespace co;

int malloc_c = 0;
int free_c = 0;

void* my_malloc(size_t size)
{
    ++ malloc_c;
    return ::malloc(size);
}
void my_free(void *ptr)
{
    ++ free_c;
    ::free(ptr);
}

TEST(testMallocFree, testMallocFree)
{
    co_opt.stack_malloc_fn = &my_malloc;
    co_opt.stack_free_fn = &my_free;

    EXPECT_EQ(malloc_c, 0);
    EXPECT_EQ(free_c, 0);

    go []{};

    EXPECT_EQ(malloc_c, 1);
    EXPECT_EQ(free_c, 0);

    WaitUntilNoTask();

    EXPECT_EQ(malloc_c, 1);
    EXPECT_EQ(free_c, 1);

    for (int i = 0; i < 10; ++i)
        go_stack(8192) []{};

    EXPECT_EQ(malloc_c, 11);

    WaitUntilNoTask();

    EXPECT_EQ(malloc_c, 11);

    go []{
        go []{};
    };

    EXPECT_EQ(malloc_c, 12);

    WaitUntilNoTask();

    EXPECT_EQ(malloc_c, 13);
}
