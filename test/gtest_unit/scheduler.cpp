#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

Scheduler & sched2() {
    static Scheduler *obj = Scheduler::Create();
    return *obj;
}

TEST(MultiScheduler, multiScheduler)
{
    Scheduler & sched = sched2();
    int val = 0;
    go co_scheduler(sched) [&]{
        ++ val;
    };

    int nTaskCount = sched.TaskCount();
    EXPECT_EQ(nTaskCount, 1);
    startScheduler ss1(sched);
    WaitUntilNoTaskS(sched);
    EXPECT_EQ(val, 1);
}
