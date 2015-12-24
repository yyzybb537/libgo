#include "thread_pool.h"
#include <unistd.h>
#include "scheduler.h"

namespace co {

ThreadPool::~ThreadPool()
{
    TPElemBase *elem = NULL;
    while ((elem = elem_list_.pop())) {
        delete elem;
    }
}

uint32_t ThreadPool::Run()
{
    uint32_t c = 0;
    TPElemBase *elem = NULL;
    while ((elem = elem_list_.pop())) {
        elem->Do();
        delete elem;
    }

    if (!c) {
        uint8_t sleep_ms = (std::min)(sleep_ms_++, g_Scheduler.GetOptions().max_sleep_ms);
        usleep(sleep_ms * 1000);
    } else {
        sleep_ms_ = 0;
    }

    return c;
}

void ThreadPool::RunLoop()
{
    for (;;)
        Run();
}

} //namespace co
