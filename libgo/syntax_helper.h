#pragma once
#include <libgo/scheduler.h>
#include <libgo/channel.h>
#include <libgo/thread_pool.h>
#include <libgo/co_rwmutex.h>
#include <libgo/debugger.h>
#include <libgo/co_local_storage.h>
#if __linux__
#include "linux_glibc_hook.h"
#endif

namespace co
{

struct __go_option_all
{
    const char* file_ = nullptr;
    int lineno_ = 0;
    int dispatch_ = egod_default;
    std::size_t stack_size_ = 0;
    bool affinity_ = false;
};

enum {
    opt_stack_size,
    opt_dispatch,
    opt_affinity,
};

template <int OptType>
struct __go_option;

template <>
struct __go_option<opt_stack_size>
{
    std::size_t stack_size_;
    explicit __go_option(std::size_t v) : stack_size_(v) {}
};

template <>
struct __go_option<opt_dispatch>
{
    int dispatch_;
    explicit __go_option(int v) : dispatch_(v) {}
};

template <>
struct __go_option<opt_affinity>
{
    bool affinity_;
    explicit __go_option(bool v) : affinity_(v) {}
};

struct __go
{
    __go(const char* file, int lineno)
    {
        opt_.file_ = file;
        opt_.lineno_ = lineno;
    }

    template <typename Function>
    ALWAYS_INLINE void operator-(Function const& f)
    {
        Scheduler::getInstance().CreateTask(f, opt_.stack_size_,
                opt_.file_, opt_.lineno_, opt_.dispatch_, opt_.affinity_);
    }

    ALWAYS_INLINE __go& operator-(__go_option<opt_stack_size> const& opt)
    {
        opt_.stack_size_ = opt.stack_size_;
        return *this;
    }

    ALWAYS_INLINE __go& operator-(__go_option<opt_dispatch> const& opt)
    {
        opt_.dispatch_ = opt.dispatch_;
        return *this;
    }

    ALWAYS_INLINE __go& operator-(__go_option<opt_affinity> const& opt)
    {
        opt_.affinity_ = opt.affinity_;
        return *this;
    }

    __go_option_all opt_;
};

// co_channel
template <typename T>
using co_chan = Channel<T>;

// co_timer_add will returns timer_id; The timer_id type is co::TimerId, it's a shared_ptr.
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

typedef	::co::Scheduler::TaskListener co_listener;

} //namespace co

