#include <iostream>
#include <gtest/gtest.h>
#include <chrono>
#include <boost/thread.hpp>
#include "gtest_exit.h"
#include "coroutine.h"
using namespace std;
using namespace co;

TEST(testReenter, testReenter)
{
    bool run = false;
    go [&]{
        co_sched.RunUntilNoTask();
        co_sched.Run();
        co_sched.RunLoop();
        run = true;
    };

    co_sched.RunUntilNoTask();
    EXPECT_TRUE(run);
}
