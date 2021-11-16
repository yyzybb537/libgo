#pragma once
#define __const__
#include "common/config.h"
#include "common/pp.h"
#include "common/syntax_helper.h"

#include "sync/channel.h"
#include "sync/co_mutex.h"
#include "sync/co_rwmutex.h"

#if USE_ROUTINE_SYNC
# include "routine_sync/condition_variable.h"
#endif //USE_ROUTINE_SYNC

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

// co_chan
using ::co::co_chan;

#if USE_ROUTINE_SYNC
using co_condition_variable = ::libgo::ConditionVariable;
#endif //USE_ROUTINE_SYNC

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

