#include <boost/thread.hpp>
#include <boost/progress.hpp>
#include "coroutine.h"
#include <iostream>
#include <unistd.h>
#include <mutex>
using namespace std;

static co_mutex g_mutex;
static const int co_count = 100;
static const int switch_per_co = 10;
static const int thread_count = 4;
int g_int = 0;

void foo()
{
//        std::unique_lock<co_mutex> lk(g_mutex);
    for (int i = 0; i < switch_per_co; ++i) {
        std::unique_lock<co_mutex> lk(g_mutex);
        printf("%d\n", ++g_int);
    }
}

int main(int argc, char** argv)
{
    g_Scheduler.GetOptions().debug = co::dbg_syncblock;
    for (int i = 0; i < co_count; ++i)
        go foo;
    cout << "go" << endl;

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
    cout << "end" << endl;
    return 0;
}
