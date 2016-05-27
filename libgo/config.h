#pragma once
#include "cmake_config.h"
#include <chrono>

// VS2013不支持thread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
# define thread_local __declspec(thread)
# define UNSUPPORT_STEADY_TIME
#endif

#if __linux__
#include <unistd.h>
#include <sys/types.h>
#endif

namespace co
{

///---- debugger flags
static const uint64_t dbg_none              = 0;
static const uint64_t dbg_all               = ~(uint64_t)0;
static const uint64_t dbg_hook              = 0x1;
static const uint64_t dbg_yield             = 0x1 << 1;
static const uint64_t dbg_scheduler         = 0x1 << 2;
static const uint64_t dbg_task              = 0x1 << 3;
static const uint64_t dbg_switch            = 0x1 << 4;
static const uint64_t dbg_ioblock           = 0x1 << 5;
static const uint64_t dbg_wait              = 0x1 << 6;
static const uint64_t dbg_exception         = 0x1 << 7;
static const uint64_t dbg_syncblock         = 0x1 << 8;
static const uint64_t dbg_timer             = 0x1 << 9;
static const uint64_t dbg_scheduler_sleep   = 0x1 << 10;
static const uint64_t dbg_sleepblock        = 0x1 << 11;
static const uint64_t dbg_spinlock          = 0x1 << 12;
static const uint64_t dbg_fd_ctx            = 0x1 << 13;
static const uint64_t dbg_debugger          = 0x1 << 14;
static const uint64_t dbg_sys_max           = dbg_debugger;
///-------------------

#if __linux__
	typedef std::chrono::nanoseconds MininumTimeDurationType;
#else
	typedef std::chrono::microseconds MininumTimeDurationType;
#endif

// 将协程分派到线程中的策略
enum e_go_dispatch
{
    egod_default = -4,  // if enable_work_steal, it's equal egod_local_thread; else, equal egod_robin.
    egod_random = -3,
    egod_robin = -2,
    egod_local_thread = -1,

    // ...
};

extern uint64_t codebug_GetDebugOptions();
extern FILE* codebug_GetDebugOutput();
extern uint32_t codebug_GetCurrentProcessID();
extern uint32_t codebug_GetCurrentThreadID();
extern std::string codebug_GetCurrentTime();

} //namespace co

#define DebugPrint(type, fmt, ...) \
    do { \
        if (::co::codebug_GetDebugOptions() & (type)) { \
            fprintf(::co::codebug_GetDebugOutput(), "co_dbg[%s][%08u][%04u] " fmt "\n", \
                    ::co::codebug_GetCurrentTime().c_str(),\
                    ::co::codebug_GetCurrentProcessID(), ::co::codebug_GetCurrentThreadID(), ##__VA_ARGS__); \
        } \
    } while(0)

