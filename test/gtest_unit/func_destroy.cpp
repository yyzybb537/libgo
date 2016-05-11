#include <stdio.h>
#include <iostream>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include "gtest_exit.h"
using namespace std;
using namespace co;

bool is_in = false;
struct A
{
    A() { ++sc; }
    A(const A&) { ++sc; }
    ~A() {
        --sc;
        if (is_in) {    // 析构在协程中执行
            EXPECT_TRUE(co_sched.IsCoroutine());
        }
    }

    static int sc;
};
int A::sc = 0;

void foo(A a)
{
    printf("foo\n");
}

TEST(testDestroy, testDestroy)
{
    A a;
    EXPECT_EQ(A::sc, 1);
    go [a] {
        foo(a);
    };
    EXPECT_EQ(A::sc, 2);
    is_in = true;
    co_sched.RunUntilNoTask();
    is_in = false;
    EXPECT_EQ(A::sc, 1);
}
