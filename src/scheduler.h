#pragma once
#include <ucontext.h>
#include <unordered_map>
#include <list>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include "task.h"
#include "block_object.h"
#include "co_mutex.h"
#include "timer.h"
#include "io_wait.h"
#include "sleep_wait.h"
#include "processer.h"

#define DebugPrint(type, fmt, ...) \
    do { \
        if (g_Scheduler.GetOptions().debug & type) { \
            fprintf(g_Scheduler.GetOptions().debug_output, "co_dbg[%04d] " fmt "\n", \
                    g_Scheduler.GetCurrentThreadID(), ##__VA_ARGS__); \
        } \
    } while(0)

namespace co
{

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
static const uint64_t dbg_sys_max           = dbg_sleepblock;
///-------------------

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
    // 调试选项, 例如: dbg_switch 或 dbg_hook|dbg_task|dbg_wait
    uint64_t debug = 0;            

    // 调试信息输出位置，改写这个配置项可以重定向输出位置
    FILE* debug_output = stdout;   

    // 协程中抛出未捕获异常时的处理方式
    eCoExHandle exception_handle = eCoExHandle::immedaitely_throw;

    // 协程栈大小上限, 只会影响在此值设置之后新创建的P, 建议在首次Run前设置.
    // @仅在Linux生效.
    uint32_t stack_size = 2 * 1024 * 1024; 

    // 初始协程栈大小, 此值越小, 对内存消耗越少, 但不会低于16bytes.
    //    设置一个恰当的初始栈大小, 可以避免栈内存重分配, 提高性能, 但可能会浪费一部分内存.
    //    这个值只是用来保存栈内存的内存块初始大小, 即使设置的很大, 栈大小也不会超过stack_size
    // @仅在Linux生效.
    uint32_t init_stack_size = 512; 

    // P的数量, 首次Run时创建所有P, 随后只能增加新的P不能减少现有的P
    //    此值越大, 并行效果越好, 但是相应的每次Run时的消耗很会增加, 同时会占用大量内存.
    //    建议设置为Run线程数的两倍或四倍.
    uint32_t processer_count = 16;

    // 没有协程需要调度时, Run最多休眠的毫秒数
    uint8_t max_sleep_ms = 20;
};
///-------------------

struct ThreadLocalInfo
{
    Task* current_task = NULL;
    ucontext_t scheduler;
    uint32_t thread_id = 0;     // Run thread index, increment from 1.
};

class ThreadPool;

class Scheduler
{
    public:
        typedef TSQueue<Task> TaskList;  // 线程安全的协程队列
        typedef TSQueue<Processer> ProcList;
        typedef std::pair<uint32_t, TSQueue<Task, false>> WaitPair;
        typedef std::unordered_map<uint64_t, WaitPair> WaitZone;
        typedef std::unordered_map<int64_t, WaitZone> WaitTable;

        static Scheduler& getInstance();

        // 获取配置选项
        CoroutineOptions& GetOptions();

        // 创建一个协程
        void CreateTask(TaskF const& fn);

        // 当前是否处于协程中
        bool IsCoroutine();

        // 是否没有协程可执行
        bool IsEmpty();

        // 当前协程让出执行权
        void CoYield();

        // 调度器调度函数, 内部执行协程、调度协程
        uint32_t Run();

        // 循环Run直到没有协程为止
        void RunUntilNoTask();
        
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

    public:
        /// sleep switch
        //  \timeout_ms min value is 0.
        void SleepSwitch(int timeout_ms);

        /// ------------------------------------------------------------------------
        // @{ 以计数的方式模拟实现的协程同步方式. 
        //    初始计数为0, Wait减少计数, Wakeup增加计数.
        //    UserBlockWait将阻塞式（yield）地等待计数大于0, 等待成功后将计数减一,
        //        并将协程切换回可执行状态. 如果不在协程中调用, 则返回false, 且不做任何事.
        //    TryBlockWait检查当前计数, 如果计数等于0, 则返回false; 否则计数减一并返回true.
        //    UserBlockWakeup检查当前等待队列, 将等待队列中的前面最多wakeup_count个
        //        协程唤醒（设置为可执行状态）, 累加剩余计数（wakeup_count减去唤醒的协程数量）
        //
        // 用户自定义的阻塞切换, type范围限定为: [0, 0xffffffff]
        bool UserBlockWait(uint32_t type, uint64_t wait_id);
        bool TryUserBlockWait(uint32_t type, uint64_t wait_id);
        uint32_t UserBlockWakeup(uint32_t type, uint64_t wait_id, uint32_t wakeup_count = 1);
        // }@
        /// ------------------------------------------------------------------------
        
        /// ------------------------------------------------------------------------
        // @{ 定时器
        TimerId ExpireAt(CoTimerMgr::TimePoint const& time_point, CoTimer::fn_t const& fn);

        template <typename Duration>
        TimerId ExpireAt(Duration const& duration, CoTimer::fn_t const& fn)
        {
            return this->ExpireAt(CoTimerMgr::Now() + duration, fn);
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

    public:
        Task* GetCurrentTask();

        /// 调用阻塞式网络IO时, 将当前协程加入等待队列中, socket加入epoll中.
        void IOBlockSwitch(int fd, uint32_t event, int timeout_ms);
        void IOBlockSwitch(std::vector<FdStruct> && fdsts, int timeout_ms);

    private:
        Scheduler();
        ~Scheduler();

        Scheduler(Scheduler const&) = delete;
        Scheduler(Scheduler &&) = delete;
        Scheduler& operator=(Scheduler const&) = delete;
        Scheduler& operator=(Scheduler &&) = delete;

        // 将一个协程加入可执行队列中
        void AddTaskRunnable(Task* tk);

        /// ------------------------------------------------------------------------
        // 协程框架定义的阻塞切换, type范围不可与用户自定义范围重叠, 指定为:[-xxxxx, -1]
        // 如果不在协程中调用, 则返回false, 且不做任何事.
        bool BlockWait(int64_t type, uint64_t wait_id);

        // 尝试等待某个事件发生, 功能等同于try_lock, 可在协程外调用.
        bool TryBlockWait(int64_t type, uint64_t wait_id);

        // 唤醒对某个时间等待的协程.
        uint32_t BlockWakeup(int64_t type, uint64_t wait_id, uint32_t wakeup_count = 1);
        // @
        /// ------------------------------------------------------------------------

        // 清理没有等待也没有被等待的WaitPair.
        void ClearWaitPairWithoutLock(int64_t type, uint64_t wait_id, WaitZone& zone, WaitPair& wait_pair);

    private:
        // Run函数的一部分, 处理runnable状态的协程
        uint32_t DoRunnable();

        // Run函数的一部分, 处理epoll相关
        int DoEpoll();

        // Run函数的一部分, 处理sleep相关
        uint32_t DoSleep();

        // Run函数的一部分, 处理定时器
        uint32_t DoTimer();

        // 获取线程局部信息
        ThreadLocalInfo& GetLocalInfo();

        // List of Processer
        LFLock proc_init_lock_;
        uint32_t proc_count = 0;
        ProcList run_proc_list_;
        ProcList wait_proc_list_;

        // List of task.
        TaskList run_tasks_;

        // io block waiter.
        IoWait io_wait_;

        // sleep block waiter.
        SleepWait sleep_wait_;

        // User define wait tasks table.
        WaitTable user_wait_tasks_;
        LFLock user_wait_lock_;

        // Timer manager.
        CoTimerMgr timer_mgr_;

        ThreadPool *thread_pool_;

        std::atomic<uint32_t> task_count_{0};
        std::atomic<uint8_t> sleep_ms_{0};
        std::atomic<uint32_t> thread_id_{0};

    friend class CoMutex;
    friend class BlockObject;
    friend class IoWait;
    friend class SleepWait;
    friend class Processer;
};

} //namespace co

#define g_Scheduler ::co::Scheduler::getInstance()

