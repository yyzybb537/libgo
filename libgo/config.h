#pragma once
#include <libgo/cmake_config.h>
#include <unordered_map>
#include <list>
#ifdef __linux__
#include <sys/epoll.h>
#endif
#ifdef __APPLE__
//...
#endif
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

// VS2013ﾲﾻￖﾧﾳￖthread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
# define thread_local __declspec(thread)
# define UNSUPPORT_STEADY_TIME
#endif

#if defined(__GNUC__) && (__GNUC__ > 3 ||(__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
# define ALWAYS_INLINE __attribute__ ((always_inline))
#else
# define ALWAYS_INLINE inline
#endif

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

#ifdef __linux__
#define THREAD_TLS thread_local
#elif defined (__APPLE__)
#define THREAD_TLS __thread
#elif defined (_MSC_VER)
#define THREAD_TLS __declspec(thread)
#else // !C++11 && !__GNUC__ && !_MSC_VER
#error "Define a thread local storage qualifier for your compiler/platform!"
#endif
    
#ifdef __APPLE__
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
    
static const short EPOLLIN       = 0x0001;
static const short EPOLLOUT      = 0x0004;
static const short EPOLLERR      = 0x0008;
static const short EPOLLHUP      = 0x0010;
static const short EPOLLNVAL     = 0x0020;
static const short EPOLLREMOVE   = 0x0800;

#define sighandler_t sig_t
#define __SIGRTMAX 32
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
static const uint64_t dbg_sys_max           = dbg_debugger;
///-------------------

#if __linux__
	typedef std::chrono::nanoseconds MininumTimeDurationType;
#else
	typedef std::chrono::microseconds MininumTimeDurationType;
#endif

// ﾽﾫ￐ﾭﾳￌﾷￖￅ￉ﾵﾽￏ￟ﾳￌￖ￐ﾵￄﾲ￟ￂￔ
enum e_go_dispatch
{
    egod_default = -4,  // if enable_work_steal, it's equal egod_local_thread; else, equal egod_robin.
    egod_random = -3,
    egod_robin = -2,
    egod_local_thread = -1,

    // ...
};

// ￐ﾭﾳￌￖ￐ￅￗﾳ￶ￎﾴﾲﾶﾻ￱ￒ￬ﾳﾣￊﾱﾵￄﾴﾦ￀￭ﾷﾽￊﾽ
enum class eCoExHandle : uint8_t
{
    immedaitely_throw,  // ￁ﾢﾼﾴￅￗﾳ￶
    delay_rethrow,      // ￑ￓﾳ￙ﾵﾽﾵ￷ﾶ￈ￆ￷ﾵ￷ﾶ￈ￊﾱￅￗﾳ￶
    debugger_only,      // ﾽ￶ﾴ￲ￓﾡﾵ￷ￊￔ￐ￅￏﾢ
};

typedef void*(*stack_malloc_fn_t)(size_t size);
typedef void(*stack_free_fn_t)(void *ptr);

///---- ￅ￤ￖￃ￑ﾡￏ￮
struct CoroutineOptions
{
    /*********************** Debug options **********************/
    // ﾵ￷ￊￔ￑ﾡￏ￮, ￀�￈￧: dbg_switch ﾻ￲ dbg_hook|dbg_task|dbg_wait
    uint64_t debug = 0;            

    // ﾵ￷ￊￔ￐ￅￏﾢￊ￤ﾳ￶ￎﾻￖￃﾣﾬﾸￄ￐ﾴￕ￢ﾸ￶ￅ￤ￖￃￏ￮﾿￉ￒￔￖ￘ﾶﾨￏ￲ￊ￤ﾳ￶ￎﾻￖￃ
    FILE* debug_output = stdout;   
    /************************************************************/

    /**************** Stack and Exception options ***************/
    // ￐ﾭﾳￌￖ￐ￅￗﾳ￶ￎﾴﾲﾶﾻ￱ￒ￬ﾳﾣￊﾱﾵￄﾴﾦ￀￭ﾷﾽￊﾽ
    eCoExHandle exception_handle = eCoExHandle::immedaitely_throw;

    // ￐ﾭﾳￌￕﾻﾴ￳￐ﾡ￉ￏￏ￞, ￖﾻﾻ￡ￓﾰￏ￬ￔￚﾴￋￖﾵ￉￨ￖￃￖﾮﾺ￳￐ￂﾴﾴﾽﾨﾵￄP, ﾽﾨￒ￩ￔￚￊￗﾴￎRunￇﾰ￉￨ￖￃ.
    // stack_sizeﾽﾨￒ￩￉￨ￖￃﾲﾻﾳﾬﾹ�1MB
    // Linuxￏﾵￍﾳￏￂ, ￉￨ￖￃ2MBﾵￄstack_sizeﾻ￡ﾵﾼￖￂￌ￡ﾽﾻￄￚﾴ￦ﾵￄￊﾹￓￃ￁﾿ﾱ￈1MBﾵￄstack_sizeﾶ￠10ﾱﾶ.
    uint32_t stack_size = 1 * 1024 * 1024; 

    // ￃﾻￓ￐￐ﾭﾳￌ￐￨ￒﾪﾵ￷ﾶ￈ￊﾱ, Runￗ￮ﾶ￠￐￝ￃ￟ﾵￄﾺ￁ￃ￫ￊ�(﾿ﾪﾷﾢﾸ￟ￊﾵￊﾱ￐ￔￏﾵￍﾳ﾿￉ￒￔ﾿ﾼￂￇﾵ￷ﾵￍￕ￢ﾸ￶ￖﾵ)
    uint8_t max_sleep_ms = 20;

    // ￃ﾿ﾸ￶ﾶﾨￊﾱￆ￷ￃ﾿ￖﾡﾴﾦ￀￭ﾵￄ￈ￎￎ￱ￊ�￁﾿(ￎﾪ0ﾱ￭ￊﾾﾲﾻￏ￞, ￃ﾿ￖﾡﾴﾦ￀￭ﾵﾱￇﾰￋ￹ￓ￐﾿￉ￒￔﾴﾦ￀￭ﾵￄ￈ￎￎ￱)
    uint32_t timer_handle_every_cycle = 0;

    // epollￃ﾿ﾴￎﾴﾥﾷﾢﾵￄeventￊ�￁﾿(Windowsￏￂￎ￞￐ﾧ)
    uint32_t epoll_event_size = 10240;

    // ￊￇﾷ￱ￆ￴ￓￃworkstealￋ￣ﾷﾨ
    bool enable_work_steal = true;

    // ￊￇﾷ￱ￆ￴ￓￃ￐ﾭﾳￌￍﾳﾼￆﾹﾦￄￜ(ﾻ￡ￓ￐ￒﾻﾵ￣￐ￔￄￜￋ￰ﾺￄ, ￄﾬ￈ￏﾲﾻ﾿ﾪￆ￴)
    bool enable_coro_stat = false;

    // ￕﾻﾶﾥ￉￨ￖￃﾱﾣﾻﾤￄￚﾴ￦ﾶￎﾵￄￄￚﾴ￦ￒﾳￊ�￁﾿(ﾽ￶linuxￏￂￓ￐￐ﾧ)(ￄﾬ￈ￏￎﾪ0, ﾼﾴ:ﾲﾻ￉￨ￖￃ)
    // ￔￚￕﾻﾶﾥￄￚﾴ￦ﾶￔￆ￫ﾺ￳ﾵￄￇﾰﾼﾸￒﾳ￉￨ￖￃￎﾪprotectￊ￴￐ￔ.
    // ￋ￹ￒￔ﾿ﾪￆ￴ﾴￋ￑ﾡￏ￮ￊﾱ, stack_sizeﾲﾻￄￜ￉￙ￓￚprotect_stack_page+1ￒﾳ
    uint32_t & protect_stack_page;

    // ￉￨ￖￃￕﾻￄￚﾴ￦ﾹￜ￀￭(malloc/free)
    // ￊﾹￓￃfiberￗ￶￐ﾭﾳￌﾵￗﾲ￣ￊﾱￎ￞￐ﾧ
    stack_malloc_fn_t & stack_malloc_fn;
    stack_free_fn_t & stack_free_fn;

    CoroutineOptions();

    inline static CoroutineOptions& getInstance()
    {
        static CoroutineOptions obj;
        return obj;
    }
};

extern uint32_t codebug_GetCurrentProcessID();
extern uint32_t codebug_GetCurrentThreadID();
extern std::string codebug_GetCurrentTime();
extern const char* BaseFile(const char* file);

} //namespace co

#define DebugPrint(type, fmt, ...) \
    do { \
        if (::co::CoroutineOptions::getInstance().debug & (type)) { \
            fprintf(::co::CoroutineOptions::getInstance().debug_output, "[%s][%05u][%02u]%s:%d:(%s)\t " fmt "\n", \
                    ::co::codebug_GetCurrentTime().c_str(),\
                    ::co::codebug_GetCurrentProcessID(), ::co::codebug_GetCurrentThreadID(), \
                    ::co::BaseFile(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            fflush(::co::CoroutineOptions::getInstance().debug_output); \
        } \
    } while(0)

