#pragma once
#include <libgo/scheduler.h>
#include <libgo/channel.h>
#include <libgo/thread_pool.h>
#include <libgo/co_rwmutex.h>
#include <libgo/debugger.h>
#if __linux__
#include "linux_glibc_hook.h"
#endif

namespace co
{

struct __go
{
    __go() = default;

    __go(const char* file, int lineno) : file_(file), lineno_(lineno) {}

    __go(const char* file, int lineno, std::size_t stack_size)
        : file_(file), lineno_(lineno), stack_size_(stack_size) {}

    __go(const char* file, int lineno, std::size_t stack_size, int dispatch)
        : file_(file), lineno_(lineno), stack_size_(stack_size), dispatch_(dispatch) {}

    template <typename Arg>
    ALWAYS_INLINE void operator-(Arg const& arg)
    {
        Scheduler::getInstance().CreateTask(arg, stack_size_, file_, lineno_, dispatch_);
    }

    const char* file_ = nullptr;
    int lineno_ = 0;
    std::size_t stack_size_ = 0;
    int dispatch_ = egod_default;
};

// co_channel
template <typename T>
using co_chan = Channel<T>;

// co_timer_add will returns timer_id; The timer_id type is uint64_t.
template <typename Arg, typename F>
inline TimerId co_timer_add(Arg const& duration_or_timepoint, F const& callback) {
    return g_Scheduler.ExpireAt(duration_or_timepoint, callback);
}

// co_timer_cancel will returns boolean type;
//   if cancel successfully it returns true,
//   else it returns false;
inline bool co_timer_cancel(TimerId timer_id) {
    return g_Scheduler.CancelTimer(timer_id);
}

// co_timer_block_cancel will returns boolean type;
//   if cancel successfully it returns true,
//   else it returns false;
//
// This function will block wait timer occurred done, if cancel error.
inline bool co_timer_block_cancel(TimerId timer_id) {
    return g_Scheduler.BlockCancelTimer(timer_id);
}

template <typename R>
struct __async_wait
{
    R result_;
    Channel<R> ch_;

    __async_wait() : ch_(1) {}

    template <typename F>
    ALWAYS_INLINE R&& operator-(F const& fn)
    {
        g_Scheduler.GetThreadPool().AsyncWait<R>(ch_, fn);
        ch_ >> result_;
        return std::move(result_);
    }
};

template <>
struct __async_wait<void>
{
    Channel<void> ch_;

    template <typename F>
    ALWAYS_INLINE void operator-(F const& fn)
    {
        g_Scheduler.GetThreadPool().AsyncWait<void>(ch_, fn);
        ch_ >> nullptr;
    }
};

} //namespace co

using ::co::egod_default;
using ::co::egod_random;
using ::co::egod_robin;
using ::co::egod_local_thread;

#define go ::co::__go(__FILE__, __LINE__)-
#define go_stack(size) ::co::__go(__FILE__, __LINE__, size)-
#define go_dispatch(dispatch) ::co::__go(__FILE__, __LINE__, 0, dispatch)-

#define co_yield do { g_Scheduler.CoYield(); } while (0)

// coroutine sleep, never blocks current thread.
#define co_sleep(milliseconds) do { g_Scheduler.SleepSwitch(milliseconds); } while (0)

// co_sched
#define co_sched g_Scheduler

// co_mutex
using ::co::co_mutex;

// co_rwmutex
using ::co::co_rwmutex;
using ::co::co_rmutex;
using ::co::co_wmutex;

// co_chan
using ::co::co_chan;

// co timer *
typedef ::co::TimerId co_timer_id;
using ::co::co_timer_add;
using ::co::co_timer_cancel;
using ::co::co_timer_block_cancel;

// co_await
#define co_await(type) ::co::__async_wait<type>()-

// co_main
#define co_main(...) extern "C" int __coroutine_main_function(__VA_ARGS__)

// co_debugger
#define co_debugger ::co::CoDebugger::getInstance()

