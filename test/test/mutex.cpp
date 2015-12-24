#include "coroutine.h"
#include <iostream>
#include <unistd.h>
using namespace std;

static co_mutex g_mutex;

void f2()
{
    g_mutex.unlock();
    cout << 1 << endl;
}

void f1()
{
    go f2;
    g_mutex.lock();
    cout << 2 << endl;
}

int main()
{
    g_Scheduler.GetOptions().debug = co::dbg_wait;
    g_mutex.try_lock();
    go f1;
    cout << "go" << endl;
    while (!g_Scheduler.IsEmpty()) {
        g_Scheduler.Run();
    }
    cout << "end" << endl;
    return 0;
}
