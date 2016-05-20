#pragma once
#include <unordered_map>
#include <list>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <deque>
#include <cstdlib>
#include "config.h"
#include "context.h"
#include "task.h"
#include "block_object.h"
#include "co_mutex.h"
#include "timer.h"
#include "io_wait.h"
#include "sleep_wait.h"
#include "processer.h"
#include "debugger.h"

namespace co {

// 协程中抛出未捕获异常时的处理方式
enum class eCoExHandle : uint8_t
{
    immedaitely_throw,  // 立即抛出
    delay_rethrow,      // 延迟到调度器调度时抛出
    debugger_only,      // 仅打印调试信息
};

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

    // 没有协程需要调度时, Run最多休眠的毫秒数(开发高实时性系统可以考虑调低这个值)
    uint8_t max_sleep_ms = 20;

    // 每个定时器每帧处理的任务数量(为0表示不限, 每帧处理当前所有可以处理的任务)
    uint32_t timer_handle_every_cycle = 0;

    // epoll每次触发的event数量(Windows下无效)
    uint32_t epoll_event_size = 10240;

    // 是否启用worksteal算法
    bool enable_work_steal = true;

    // 是否启用协程统计功能(会有一点性能损耗, 默认不开启)
    bool enable_coro_stat = false;

    // 栈顶设置保护内存段的内存页数量(仅linux下有效)(默认为0, 即:不设置)
    // 在栈顶内存对齐后的前几页设置为protect属性.
    // 所以开启此选项时, stack_size不能少于protect_stack_page+1页
    uint32_t & protect_stack_page = StackAllocator::get_protect_stack_page();

    // 设置栈内存管理(malloc/free)
    // 使用fiber做协程底层时无效
    stack_malloc_fn_t & stack_malloc_fn = StackAllocator::get_malloc_fn();
    stack_free_fn_t & stack_free_fn = StackAllocator::get_free_fn();
};
///-------------------

struct ThreadLocalInfo
{
    int thread_id = -1;     // Run thread index, increment from 1.
    uint8_t sleep_ms = 0;
    Processer *proc = nullptr;
};

class ThreadPool;

class Scheduler
{
    public:
        // Run时执行的内容
        enum eRunFlags
        {
            erf_do_coroutines = 0x1,
            erf_do_timer = 0x2,
            erf_do_sleeper = 0x4,
            erf_do_eventloop = 0x8,
            erf_idle_cpu = 0x10,
            erf_all = 0x7fffffff,
        };

        typedef std::deque<Processer*> ProcList;
        typedef std::pair<uint32_t, TSQueue<Task, false>> WaitPair;
        typedef std::unordered_map<uint64_t, WaitPair> WaitZone;
        typedef std::unordered_map<int64_t, WaitZone> WaitTable;

        static Scheduler& getInstance();

        // 获取配置选项
        CoroutineOptions& GetOptions();

        // 创建一个协程
        void CreateTask(TaskF const& fn, std::size_t stack_size,
                const char* file, int lineno, int dispatch);

        // 当前是否处于协程中
        bool IsCoroutine();

        // 是否没有协程可执行
        bool IsEmpty();

        // 当前协程让出执行权
        void CoYield();

        // 调度器调度函数, 内部执行协程、调度协程
        uint32_t Run(int flags = erf_all);

        // 循环Run直到没有协程为止
        // @loop_task_count: 不计数的常驻协程.
        //    例如：loop_task_count == 2时, 还剩最后2个协程的时候这个函数就会return.
        // @remarks: 这个接口会至少执行一次Run.
        void RunUntilNoTask(uint32_t loop_task_count = 0);
        
        // 无限循环执行Run
        void RunLoop();

        // 当前协程总数量
        uint32_t TaskCount();

        // 当前协程ID, ID从1开始（不在协程中则返回0）
        uint64_t GetCurrentTaskID();

        // 当前协程切换的次数
        uint64_t GetCurrentTaskYieldCount();

        // 设置当前协程调试信息, 打印调试信息时将回显
        void SetCurrentTaskDebugInfo(std::string const& info);

        // 获取当前协程的调试信息, 返回的内容包括用户自定义的信息和协程ID
        const char* GetCurrentTaskDebugInfo();

        // 获取当前线程ID.(按执行调度器调度的顺序计)
        uint32_t GetCurrentThreadID();

        // 获取当前进程ID.
        uint32_t GetCurrentProcessID();

    public:
        /// sleep switch
        //  \timeout_ms min value is 0.
        void SleepSwitch(int timeout_ms);

        /// ------------------------------------------------------------------------
        // @{ 定时器
        template <typename DurationOrTimepoint>
        TimerId ExpireAt(DurationOrTimepoint const& dur_or_tp, CoTimer::fn_t const& fn)
        {
            TimerId id = timer_mgr_.ExpireAt(dur_or_tp, fn);
            DebugPrint(dbg_timer, "add timer id=%llu", (long long unsigned)id->GetId());
            return id;
        }

        bool CancelTimer(TimerId timer_id);
        bool BlockCancelTimer(TimerId timer_id);
        // }@
        /// ------------------------------------------------------------------------
    
        /// ------------------------------------------------------------------------
        // @{ 线程池
        ThreadPool& GetThreadPool();
        // }@
        /// ------------------------------------------------------------------------

        // iowait对象
        IoWait& GetIoWait() { return io_wait_; }

    public:
        Task* GetCurrentTask();

    private:
        Scheduler();
        ~Scheduler();

        Scheduler(Scheduler const&) = delete;
        Scheduler(Scheduler &&) = delete;
        Scheduler& operator=(Scheduler const&) = delete;
        Scheduler& operator=(Scheduler &&) = delete;

        // 将一个协程加入可执行队列中
        void AddTaskRunnable(Task* tk, int dispatch = egod_default);

        // Run函数的一部分, 处理runnable状态的协程
        uint32_t DoRunnable(bool allow_steal = true);

        // Run函数的一部分, 处理epoll相关
        int DoEpoll(int wait_milliseconds);

        // Run函数的一部分, 处理sleep相关
        // @next_ms: 距离下一个timer触发的毫秒数
        uint32_t DoSleep(long long &next_ms);

        // Run函数的一部分, 处理定时器
        // @next_ms: 距离下一个timer触发的毫秒数
        uint32_t DoTimer(long long &next_ms);

        // 获取线程局部信息
        ThreadLocalInfo& GetLocalInfo();

        Processer* GetProcesser(std::size_t index);

        // List of Processer
        LFLock proc_init_lock_;
        ProcList run_proc_list_;
        std::atomic<uint32_t> dispatch_robin_index_{0};

        // io block waiter.
        IoWait io_wait_;

        // sleep block waiter.
        SleepWait sleep_wait_;

        // Timer manager.
        CoTimerMgr timer_mgr_;

        ThreadPool *thread_pool_;

        std::atomic<uint32_t> task_count_{0};
        std::atomic<uint32_t> thread_id_{0};

    private:
        friend class CoMutex;
        friend class BlockObject;
        friend class IoWait;
        friend class SleepWait;
        friend class Processer;
        friend class FileDescriptorCtx;
        friend class CoDebugger;
};

} //namespace co

#define g_Scheduler ::co::Scheduler::getInstance()

