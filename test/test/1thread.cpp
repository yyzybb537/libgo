#include <boost/thread.hpp>
#include <boost/progress.hpp>
#include "coroutine.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
using namespace std;

std::atomic_int g_value{0};
static const int co_count = 100000;
static const int switch_per_co = 10;

void f1()
{
    for (int i = 0; i < switch_per_co; ++i) {
        //printf("f1 %d\n", g_value++);
        g_value++;
        co_yield;
    }
}

int main()
{
    //g_Scheduler.GetOptions().debug = dbg_all;
    g_Scheduler.GetOptions().stack_size = 2048;

    for (int i = 0; i < co_count; ++i) {
        go f1;
    }

    printf("go\n");
    {
        boost::progress_timer pt;
        while (!g_Scheduler.IsEmpty()) {
            g_Scheduler.Run();
        }
        printf("main thread, run %d coroutines, %d times switch. cost ", co_count, co_count * switch_per_co);
    }
    printf("%d\nend\n", (int)g_value);
    return 0;
}

