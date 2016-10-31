#include <boost/thread.hpp>
#include <chrono>
#include <iostream>
#include <atomic>
#include <libgo/libgo.h>
#include <cmath>

#define COC 1
static uint64_t counter[COC] = {};
static uint64_t last_c = 0;
static int coro = 5;

void foo(int i)
{
    for (;;) {
        ++counter[i];
        co_yield;
    }
}

void show()
{
    uint64_t c = 0;
    for (int i = 0; i < COC; ++i)
        c += counter[i];
    uint64_t sec_c = c - last_c;
    last_c = c;
    std::cout << "Switch " << coro * coro << " times: " << sec_c << std::endl;
}

int main()
{
    
    boost::thread_group tg;
    co_sched.Run();
    for (int i = 1; i < COC; ++i)
        tg.create_thread([]{ co_sched.RunLoop(); });

    co_sched.GetOptions().enable_work_steal = false;
    tg.create_thread([]{
                for (;;) {
                    sleep(1);
                    show();
                }
            });

    for (int i = 0; i < coro*coro; ++i)
        go [=]{ foo(i % COC); };

    go []{
        for (;;) {
            for (int i = coro*coro; i < (coro+1)*(coro+1); ++i)
                go [=]{ foo(i % COC); };
            coro++;
            sleep(3);
        }
    };

    co_sched.RunLoop();
    return 0;
}


