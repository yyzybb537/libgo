#pragma once
#include "cmake_config.h"
#include <chrono>

// VS2013²»Ö§³Öthread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
#define thread_local __declspec(thread)
#endif

#ifndef _WIN32
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

#ifdef _WIN32
	typedef std::chrono::microseconds MininumTimeDurationType;
#else
	typedef std::chrono::nanoseconds MininumTimeDurationType;
#endif

extern uint64_t codebug_GetDebugOptions();
extern FILE* codebug_GetDebugOutput();
extern uint32_t codebug_GetCurrentProcessID();
extern uint32_t codebug_GetCurrentThreadID();

} //namespace co

#define DebugPrint(type, fmt, ...) \
    do { \
        if (::co::codebug_GetDebugOptions() & (type)) { \
            fprintf(::co::codebug_GetDebugOutput(), "co_dbg[%08u][%04u] " fmt "\n", \
                    ::co::codebug_GetCurrentProcessID(), ::co::codebug_GetCurrentThreadID(), ##__VA_ARGS__); \
        } \
    } while(0)

