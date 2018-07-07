#pragma once
#include "common/pp.h"
#include "common/syntax_helper.h"
//#include "defer/defer.h"

#define go ::co::__go(__FILE__, __LINE__)-

// create coroutine options
#define co_stack(size) ::co::__go_option<::co::opt_stack_size>{size}-
#define co_dispatch(thread_id_or_type) ::co::__go_option<::co::opt_dispatch>{thread_id_or_type}-
//#define co_affinity() ::co::__go_option<::co::opt_affinity>{true}-

#define go_stack(size) go co_stack(size)
#define go_dispatch(thread_id_or_type) go co_dispatch(thread_id_or_type)

#define co_yield do { g_Scheduler.CoYield(); } while (0)

// coroutine sleep, never blocks current thread.
//#define co_sleep(milliseconds) do { g_Scheduler.SleepSwitch(milliseconds); } while (0)

// co_sched
#define co_sched g_Scheduler

//// co_mutex
//using ::co::co_mutex;
//
//// co_rwmutex
//using ::co::co_rwmutex;
//using ::co::co_rmutex;
//using ::co::co_wmutex;
//
//// co_chan
//using ::co::co_chan;
//
//// co timer *
//typedef ::co::TimerId co_timer_id;
//using ::co::co_timer_add;
//using ::co::co_timer_cancel;
//using ::co::co_timer_block_cancel;
//
//// co_await
//#define co_await(type) ::co::__async_wait<type>()-

//// co_main
//#define co_main(...) extern "C" int __coroutine_main_function(__VA_ARGS__)
//
//// co_debugger
//#define co_debugger ::co::CoDebugger::getInstance()
//
//// coroutine local storage
//#define co_cls(type, ...) CLS(type, ##__VA_ARGS__)
//#define co_cls_ref(type) CLS_REF(type)
//
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
