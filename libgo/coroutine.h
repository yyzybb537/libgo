#pragma once
#define __const__
#include <atomic>
#include "common/config.h"
#include "common/pp.h"
#include "common/syntax_helper.h"
#include "sync/channel.h"
#include "sync/co_mutex.h"
#include "sync/co_rwmutex.h"
#include "timer/timer.h"
#include "scheduler/processer.h"
#include "cls/co_local_storage.h"
#include "pool/connection_pool.h"
#include "pool/async_coroutine_pool.h"
#include "defer/defer.h"
#include "debug/listener.h"
#include "debug/debugger.h"
//#include "netio/unix/errno_hook.h"

#define LIBGO_VERSION 300

#define go_alias ::co::__go(__FILE__, __LINE__)-
#define go go_alias

// create coroutine options
#define co_stack(size) ::co::__go_option<::co::opt_stack_size>{size}-
#define co_scheduler(pScheduler) ::co::__go_option<::co::opt_scheduler>{pScheduler}-

#define go_stack(size) go co_stack(size)

#define co_yield do { ::co::Processer::StaticCoYield(); } while (0)

// coroutine sleep, never blocks current thread if run in coroutine.
#if defined(LIBGO_SYS_Unix)
# define co_sleep(milliseconds) do { usleep(1000 * milliseconds); } while (0)
#else
# define co_sleep(milliseconds) do { ::Sleep(milliseconds); } while (0)
#endif

// co_sched
#define co_sched g_Scheduler

#define co_opt ::co::CoroutineOptions::getInstance()

// co_mutex
using ::co::co_mutex;

// co_rwmutex
using ::co::co_rwmutex;
using ::co::co_rmutex;
using ::co::co_wmutex;

// co_condition_variable
typedef ::co::ConditionVariableAny co_condition_variable;

// co_chan
using ::co::co_chan;

// co_timer
typedef ::co::CoTimer co_timer;
typedef ::co::CoTimer::TimerId co_timer_id;

//// co_await
//#define co_await(type) ::co::__async_wait<type>()-

//// co_debugger
//#define co_debugger ::co::CoDebugger::getInstance()

// coroutine local storage
#define co_cls(type, ...) CLS(type, ##__VA_ARGS__)
#define co_cls_ref(type) CLS_REF(type)

// co_defer
#define co_defer auto LIBGO_PP_CAT(__defer_, __COUNTER__) = ::co::__defer_op()-
#define co_last_defer() ::co::GetLastDefer()
#define co_defer_scope co_defer [&]

class CountDownLatch {
public:
    explicit CountDownLatch(size_t n = 1) : mFlyingCount(n) {}

    void Add(size_t i) {
        std::unique_lock<co_mutex> lck(mu);
        mFlyingCount += i;
    }

    void Done() {
        std::unique_lock<co_mutex> lck(mu);
        if (--mFlyingCount == 0) {
            cv.notify_all();
        }
    }
    void Wait() {
        std::unique_lock<co_mutex> lck(mu);
        while (mFlyingCount > 0) {
            cv.wait(lck);
        }
    }
private:
    size_t mFlyingCount;
    co_mutex mu;
    co_condition_variable cv;

    CountDownLatch(CountDownLatch const &) = delete;
    CountDownLatch(CountDownLatch &&) = delete;
    CountDownLatch& operator=(CountDownLatch const &) = delete;
    CountDownLatch& operator=(CountDownLatch &&) = delete;
};
