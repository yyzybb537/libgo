#pragma once
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
//#include "defer/defer.h"

#define go_alias ::co::__go(__FILE__, __LINE__)-
#define go go_alias

// create coroutine options
#define co_stack(size) ::co::__go_option<::co::opt_stack_size>{size}-
#define co_scheduler(pScheduler) ::co::__go_option<::co::opt_scheduler>{pScheduler}-

#define go_stack(size) go co_stack(size)

#define co_yield do { ::co::Processer::StaticCoYield(); } while (0)

// coroutine sleep, never blocks current thread.
#define co_sleep(milliseconds) do { usleep(1000 * milliseconds); } while (0)

// co_sched
#define co_sched g_Scheduler

#define co_opt ::co::CoroutineOptions::getInstance()

// co_mutex
using ::co::co_mutex;

// co_rwmutex
using ::co::co_rwmutex;
using ::co::co_rmutex;
using ::co::co_wmutex;

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

//// co_defer
//#define co_defer auto LIBGO_PP_CAT(__defer_, __COUNTER__) = ::co::__defer_op()-
////#define co_last_defer() LIBGO_PP_CAT(__defer_, LIBGO_PP_DEC(__COUNTER__))
//#define co_last_defer() ::co::GetLastDefer()
//#define co_defer_scope co_defer [&]
//
//// co_listener
//using ::co::co_listener;
//
//inline void set_co_listener(::co::co_listener* listener) {
//    g_Scheduler.SetTaskListener(listener);
//}
//inline ::co::co_listener* get_co_listener() {
//    return g_Scheduler.GetTaskListener();
//}
