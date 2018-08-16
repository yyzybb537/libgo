#include "config.h"
#include "../context/fcontext.h"
#include "../scheduler/processer.h"
#include <string.h>
#include <poll.h>
#ifdef __linux__
#include <sys/time.h>
#endif

#include "../scheduler/ref.h"
#include "../cls/co_local_storage.h"

namespace co {

static int staticInitialize()
{
    // scheduler
    TaskRefInit(Affinity);
    TaskRefInit(YieldCount);
    TaskRefInit(Location);
    TaskRefInit(DebugInfo);
    TaskRefInit(SuspendId);

    // cls
    TaskRefInit(ClsMap);
    return 0;
}

void LibgoInitialize()
{
    static int ignore = staticInitialize();
    (void)ignore;
}

std::mutex gDbgLock;

CoroutineOptions::CoroutineOptions()
    : protect_stack_page(StackTraits::GetProtectStackPageSize()),
    stack_malloc_fn(StackTraits::MallocFunc()),
    stack_free_fn(StackTraits::FreeFunc())
{
}

const char* BaseFile(const char* file)
{
    const char* p = strrchr(file, '/');
    if (p) return p + 1;

    p = strrchr(file, '\\');
    if (p) return p + 1;

    return file;
}

int GetCurrentProcessID()
{
#if __linux__
    return getpid();
#else
    return 0;
#endif 
}

int GetCurrentThreadID()
{
    auto proc = Processer::GetCurrentProcesser();
    return proc ? proc->Id() : -1;
}

std::string GetCurrentTime()
{
#if __linux__
    struct tm local;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &local);
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%06lu",
            local.tm_year+1900, local.tm_mon+1, local.tm_mday, 
            local.tm_hour, local.tm_min, local.tm_sec, tv.tv_usec);
    return std::string(buffer);
#else
    return std::string();
#endif
}

const char* PollEvent2Str(short int event)
{
    event &= POLLIN|POLLOUT|POLLERR;
    switch (event) {
        LIBGO_E2S_DEFINE(POLLIN);
        LIBGO_E2S_DEFINE(POLLOUT);
        LIBGO_E2S_DEFINE(POLLERR);
        LIBGO_E2S_DEFINE(POLLIN|POLLOUT);
        LIBGO_E2S_DEFINE(POLLIN|POLLERR);
        LIBGO_E2S_DEFINE(POLLOUT|POLLERR);
        LIBGO_E2S_DEFINE(POLLIN|POLLOUT|POLLERR);
        default:
        return "Zero";
    }
}

} //namespace co
