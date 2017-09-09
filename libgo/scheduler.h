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

    public:
        /**
         * 协程事件监听器
         * 注意：其中所有的回调方法都不允许抛出异常
         */
        class TaskListener {
        public:
            /**
             * 协程被创建时被调用
             * （注意此时并未运行在协程中）
             *
             * @prarm task_id 协程ID
             * @prarm eptr
             */
            virtual void onCreated(uint64_t task_id) noexcept {
            }

            /**
             * 协程开始运行
             * （本方法运行在协程中）
             *
             * @prarm task_id 协程ID
             * @prarm eptr
             */
            virtual void onStart(uint64_t task_id) noexcept {
            }

            /**
             * 协程正常运行结束（无异常抛出）
             * （本方法运行在协程中）
             *
             * @prarm task_id 协程ID
             */
            virtual void onCompleted(uint64_t task_id) noexcept {
            }

            /**
             * 协程抛出未被捕获的异常（本方法运行在协程中）
             * @prarm task_id 协程ID
             * @prarm eptr 抛出的异常对象指针，可对本指针赋值以修改异常对象，
             *             异常将使用 CoroutineOptions.exception_handle 中
             *             配置的方式处理；赋值为nullptr则表示忽略此异常
             *             ！！注意：当 exception_handle 配置为 immedaitely_throw 时本事件
             *             ！！与 onFinished() 均失效，异常发生时将直接抛出并中断程序的运行，同时生成coredump
             */
            virtual void onException(uint64_t task_id, std::exception_ptr& eptr) noexcept {
            }

            /**
             * 协程运行完成，if(eptr) 为false说明协程正常结束，为true说明协程抛出了了异常
             *
             * @prarm task_id 协程ID
             * @prarm eptr 抛出的异常对象指针
             */
            virtual void onFinished(uint64_t task_id, const std::exception_ptr eptr) noexcept {
            }

            virtual ~TaskListener() noexcept = default;

            //                      ---> onCompleted -->
            //                      |                  |
            // onCreated --> onStart                   ---> onFinished
            //                      |                  |
            //                      ---> onException -->
        };

    private:
        TaskListener* task_listener = nullptr;

    public:
        inline TaskListener* GetTaskListener() {
            return task_listener;
        }
        inline void SetTaskListener(TaskListener* listener) {
            this->task_listener = listener;
        }
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
