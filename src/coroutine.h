#pragma once
#include "scheduler.h"
#include "channel.h"
#include "thread_pool.h"
#include "co_rwmutex.h"

namespace co
{

struct __go
{
    template <typename Arg>
    inline void operator-(Arg const& arg)
    {
        Scheduler::getInstance().CreateTask(arg);
    }
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

    template <typename F>
    inline R&& operator-(F const& fn)
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
    inline void operator-(F const& fn)
    {
        g_Scheduler.GetThreadPool().AsyncWait<void>(ch_, fn);
        ch_ >> nullptr;
    }
};

} //namespace co

#define go ::co::__go()-
#define co_yield do { g_Scheduler.CoYield(); } while (0)

// (uint32_t type, uint64_t id)
#define co_wait(type, id) do { g_Scheduler.UserBlockWait(type, id); } while (0)
#define co_wakeup(type, id) do { g_Scheduler.UserBlockWakeup(type, id); } while (0)

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
