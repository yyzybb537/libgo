#pragma once
#include <libgo/config.h>
#include <libgo/context.h>
#include <libgo/task.h>
#include <libgo/block_object.h>
#include <libgo/co_mutex.h>
#include <libgo/timer.h>
#include <libgo/sleep_wait.h>
#include <libgo/processer.h>
#include <libgo/debugger.h>
#include "io_wait.h"

namespace co {

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
            erf_do_coroutines   = 0x1,
            erf_do_timer        = 0x2,
            erf_do_sleeper      = 0x4,
            erf_do_eventloop    = 0x8,
            erf_idle_cpu        = 0x10,
            erf_signal          = 0x20,
            erf_all             = 0x7fffffff,
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
        atomic_t<uint32_t> dispatch_robin_index_{0};

        // io block waiter.
        IoWait io_wait_;

        // sleep block waiter.
        SleepWait sleep_wait_;

        // Timer manager.
        CoTimerMgr timer_mgr_;

        ThreadPool *thread_pool_;
        LFLock thread_pool_init_;

        atomic_t<uint32_t> task_count_{0};
        atomic_t<uint32_t> thread_id_{0};

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

namespace co
{
    inline Scheduler& Scheduler::getInstance()
    {
        static Scheduler obj;
        return obj;
    }

    inline CoroutineOptions& Scheduler::GetOptions()
    {
        return CoroutineOptions::getInstance();
    }

} //namespace co
