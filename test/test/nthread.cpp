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
static int thread_count = 2;

void f1()
{
    for (int i = 0; i < switch_per_co; ++i) {
        //printf("f1 %d\n", g_value++);
        g_value++;
        co_yield;
    }
}

int main(int argc, char** argv)
{
//    g_Scheduler.GetOptions().debug = dbg_all;
    g_Scheduler.GetOptions().stack_size = 2048;

    if (argc > 1)
        thread_count = atoi(argv[1]);

    for (int i = 0; i < co_count; ++i) {
        go f1;
    }

    printf("go\n");
    {
        boost::progress_timer pt;
        boost::thread_group tg;
        for (int i = 0; i < thread_count; ++i)
        {
            tg.create_thread([] {
                    uint32_t c = 0;
                    while (!g_Scheduler.IsEmpty()) {
                        c += g_Scheduler.Run();
                    }
                    printf("[%lu] do count: %u\n", pthread_self(), c);
                });
        }
        tg.join_all();
        printf("%d threads, run %d coroutines, %d times switch. cost ",
                thread_count, co_count, co_count * switch_per_co);
    }
    printf("%d\nend\n", (int)g_value);
    return 0;
}

