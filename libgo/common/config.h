#pragma once
#include "cmake_config.h"
#include <unordered_map>
#include <list>
#include <errno.h>
#include <string.h>
#include <cstdlib>
#include <stdio.h>
#include <atomic>
#include <mutex>
#include <assert.h>
#include <deque>
#include <string>
#include <type_traits>
#include <stddef.h>
#include <exception>
#include <vector>
#include <set>
#include <map>
#include <functional>
#include <chrono>
#include <memory>
#include <queue>

#define LIBGO_DEBUG 0

// VS2013不支持thread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
# define thread_local __declspec(thread)
# define UNSUPPORT_STEADY_TIME
#endif

#if defined(__GNUC__) && (__GNUC__ > 3 ||(__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
# define ALWAYS_INLINE __attribute__ ((always_inline)) inline 
#else
# define ALWAYS_INLINE inline
#endif

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#if __linux__
#include <unistd.h>
#include <sys/types.h>
#endif

namespace co
{

#if LIBGO_SINGLE_THREAD
    template <typename T>
    using atomic_t = T;
#else
    template <typename T>
    using atomic_t = std::atomic<T>;
#endif

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
static const uint64_t dbg_signal            = 0x1 << 15;
static const uint64_t dbg_channel           = 0x1 << 16;
static const uint64_t dbg_sys_max           = dbg_debugger;
///-------------------

#if __linux__
	typedef std::chrono::nanoseconds MininumTimeDurationType;
#else
	typedef std::chrono::microseconds MininumTimeDurationType;
#endif

// 协程中抛出未捕获异常时的处理方式
enum class eCoExHandle : uint8_t
{
    immedaitely_throw,  // 立即抛出
    delay_rethrow,      // 延迟到调度器调度时抛出
    debugger_only,      // 仅打印调试信息
};

typedef void*(*stack_malloc_fn_t)(size_t size);
typedef void(*stack_free_fn_t)(void *ptr);

///---- 配置选项
struct CoroutineOptions
{
    /*********************** Debug options **********************/
    // 调试选项, 例如: dbg_switch 或 dbg_hook|dbg_task|dbg_wait
    uint64_t debug = 0;            

    // 调试信息输出位置，改写这个配置项可以重定向输出位置
    FILE* debug_output = stdout;   
    /************************************************************/

    /**************** Stack and Exception options ***************/
    // 协程中抛出未捕获异常时的处理方式
    eCoExHandle exception_handle = eCoExHandle::immedaitely_throw;

    // 协程栈大小上限, 只会影响在此值设置之后新创建的P, 建议在首次Run前设置.
    // stack_size建议设置不超过1MB
    // Linux系统下, 设置2MB的stack_size会导致提交内存的使用量比1MB的stack_size多10倍.
    uint32_t stack_size = 1 * 1024 * 1024; 
    /************************************************************/

    // epoll每次触发的event数量(Windows下无效)
    uint32_t epoll_event_size = 10240;

    // 是否启用协程统计功能(会有一点性能损耗, 默认不开启)
    bool enable_coro_stat = false;

    // 单协程执行超时时长(单位：微秒) (超过时长会强制steal剩余任务, 派发到其他线程)
    uint32_t cycle_timeout_us = 10 * 1000; 

    // 调度线程的触发频率(单位：微秒)
    uint32_t dispatcher_thread_cycle_us = 1000; 

    // 栈顶设置保护内存段的内存页数量(仅linux下有效)(默认为0, 即:不设置)
    // 在栈顶内存对齐后的前几页设置为protect属性.
    // 所以开启此选项时, stack_size不能少于protect_stack_page+1页
    int & protect_stack_page;

    // 设置栈内存管理(malloc/free)
    // 使用fiber做协程底层时无效
    stack_malloc_fn_t & stack_malloc_fn;
    stack_free_fn_t & stack_free_fn;

    CoroutineOptions();

    inline static CoroutineOptions& getInstance()
    {
        static CoroutineOptions obj;
        return obj;
    }
};

extern int codebug_GetCurrentProcessID();
extern int codebug_GetCurrentThreadID();
extern std::string codebug_GetCurrentTime();
extern const char* BaseFile(const char* file);

} //namespace co

#define DebugPrint(type, fmt, ...) \
    do { \
        if (UNLIKELY(::co::CoroutineOptions::getInstance().debug & (type))) { \
            fprintf(::co::CoroutineOptions::getInstance().debug_output, "[%s][%05d][%04d]%s:%d:(%s)\t " fmt "\n", \
                    ::co::codebug_GetCurrentTime().c_str(),\
                    ::co::codebug_GetCurrentProcessID(), ::co::codebug_GetCurrentThreadID(), \
                    ::co::BaseFile(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            fflush(::co::CoroutineOptions::getInstance().debug_output); \
        } \
    } while(0)

