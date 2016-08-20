#include <libgo/scheduler.h>
#include <libgo/error.h>
#include <stdio.h>
#include <system_error>
#include <unistd.h>
#include <libgo/thread_pool.h>
#include <time.h>
#if __linux__
#include <sys/time.h>
#endif
#if WITH_SAFE_SIGNAL
#include <libgo/hook_signal.h>
#endif

namespace co
{

extern void coroutine_hook_init();
Scheduler::Scheduler()
{
    thread_pool_ = nullptr;
    coroutine_hook_init();
}

Scheduler::~Scheduler()
{
    if (thread_pool_)
        delete thread_pool_;
}

ThreadLocalInfo& Scheduler::GetLocalInfo()
{
    static thread_local ThreadLocalInfo *info = NULL;
    if (!info)
        info = new ThreadLocalInfo();

    return *info;
}

Processer* Scheduler::GetProcesser(std::size_t index)
{
    if (run_proc_list_.size() > index)
        return run_proc_list_[index];

    std::unique_lock<LFLock> lock(proc_init_lock_);
    if (run_proc_list_.size() > index)
        return run_proc_list_[index];

    while (run_proc_list_.size() <= index)
        run_proc_list_.push_back(new Processer);

    return run_proc_list_[index];
}

void Scheduler::CreateTask(TaskF const& fn, std::size_t stack_size,
        const char* file, int lineno, int dispatch)
{
    Task* tk = new Task(fn, stack_size ? stack_size : GetOptions().stack_size, file, lineno);
    ++task_count_;
    DebugPrint(dbg_task, "task(%s) created.", tk->DebugInfo());
    AddTaskRunnable(tk, dispatch);
}

bool Scheduler::IsCoroutine()
{
    return !!GetCurrentTask();
}

bool Scheduler::IsEmpty()
{
    return task_count_ == 0;
}

void Scheduler::CoYield()
{
    ThreadLocalInfo& info = GetLocalInfo();
    if (info.proc)
        info.proc->CoYield();
}

uint32_t Scheduler::Run(int flags)
{
    ThreadLocalInfo &info = GetLocalInfo();
    if (info.thread_id < 0) {
        info.thread_id = thread_id_++;
        info.proc = GetProcesser(info.thread_id);
    }

#if LIBGO_SINGLE_THREAD
    if (info.thread_id > 0) {
        usleep(20 * 1000);
        return 0;
    }
#endif

    if (IsCoroutine()) return 0;

    uint32_t run_task_count = 0;
    if (flags & erf_do_coroutines)
        run_task_count = DoRunnable(GetOptions().enable_work_steal);

    // timer
    long long timer_next_ms = GetOptions().max_sleep_ms;
    uint32_t tm_count = 0;
    if (flags & erf_do_timer)
        tm_count = DoTimer(timer_next_ms);

    // sleep wait.
    long long sleep_next_ms = GetOptions().max_sleep_ms;
    uint32_t sl_count = 0;
    if (flags & erf_do_sleeper)
        sl_count = DoSleep(sleep_next_ms);

    // 下一次timer或sleeper触发的时间毫秒数, 休眠或阻塞等待IO事件触发的时间不能超过这个值
    long long next_ms = (std::min)(timer_next_ms, sleep_next_ms);

    // epoll
    int ep_count = -1;
    if (flags & erf_do_eventloop) {
        int wait_milliseconds = 0;
        if (run_task_count || tm_count || sl_count)
            wait_milliseconds = 0;
        else {
            wait_milliseconds = (std::min<long long>)(next_ms, GetOptions().max_sleep_ms);
            DebugPrint(dbg_scheduler_sleep, "wait_milliseconds %d ms, next_ms=%lld", wait_milliseconds, next_ms);
        }
        ep_count = DoEpoll(wait_milliseconds);
    }

    if (flags & erf_idle_cpu) {
        if (!run_task_count && ep_count <= 0 && !tm_count && !sl_count) {
            if (ep_count == -1) {
                // 此线程没有执行epoll_wait, 使用sleep降低空转时的cpu使用率
                ++info.sleep_ms;
                info.sleep_ms = (std::min)(info.sleep_ms, GetOptions().max_sleep_ms);
                info.sleep_ms = (std::min<long long>)(info.sleep_ms, next_ms);
                DebugPrint(dbg_scheduler_sleep, "sleep %d ms, next_ms=%lld", (int)info.sleep_ms, next_ms);
                usleep(info.sleep_ms * 1000);
            }
        } else {
            info.sleep_ms = 1;
        }
    }

#if WITH_SAFE_SIGNAL
    if (flags & erf_signal) {
        HookSignal::getInstance().Run();
    }
#endif

    return run_task_count;
}

void Scheduler::RunUntilNoTask(uint32_t loop_task_count)
{
    if (IsCoroutine()) return ;
    do { 
        Run();
    } while (task_count_ > loop_task_count);
}

// Run函数的一部分, 处理runnable状态的协程
uint32_t Scheduler::DoRunnable(bool allow_steal)
{
    ThreadLocalInfo& info = GetLocalInfo();

    uint32_t do_count = 0;
    uint32_t done_count = 0;
    try {
        do_count += info.proc->Run(done_count);
    } catch (...) {
        task_count_ -= done_count;
        throw ;
    }
    task_count_ -= done_count;
    DebugPrint(dbg_scheduler, "run %d coroutines, remain=%d\n",
            (int)done_count, (int)task_count_);

    // Steal-Work
    if (!do_count && !done_count && allow_steal) {
        // 没有任务了, 随机选一个线程偷取
        std::unique_lock<LFLock> lock(proc_init_lock_);
        std::size_t thread_count = run_proc_list_.size();
        lock.unlock();

        if (thread_count > 1) {
            int r = rand() % thread_count;
            if (r == info.thread_id)    // 不能选到当前线程
                r = info.thread_id > 0 ? (info.thread_id - 1) : (info.thread_id + 1);
            std::size_t steal_count = run_proc_list_[r]->StealHalf(*info.proc);
            if (steal_count) {
                return DoRunnable(false);
            }
        }
    }

    return do_count;
}

// Run函数的一部分, 处理epoll相关
int Scheduler::DoEpoll(int wait_milliseconds)
{
    return io_wait_.WaitLoop(wait_milliseconds);
}

uint32_t Scheduler::DoSleep(long long &next_ms)
{
    return sleep_wait_.WaitLoop(next_ms);
}

// Run函数的一部分, 处理定时器
uint32_t Scheduler::DoTimer(long long &next_ms)
{
    uint32_t c = 0;
    while (!GetOptions().timer_handle_every_cycle || c < GetOptions().timer_handle_every_cycle)
    {
        std::list<CoTimerPtr> timers;
        next_ms = timer_mgr_.GetExpired(timers, 128);
        if (timers.empty()) break;
        c += timers.size();
        for (auto &sp_timer : timers)
        {
            DebugPrint(dbg_timer, "enter timer callback %llu", (long long unsigned)sp_timer->GetId());
            (*sp_timer)();
            DebugPrint(dbg_timer, "leave timer callback %llu", (long long unsigned)sp_timer->GetId());
        }
    }

    return c;
}

void Scheduler::RunLoop()
{
    if (IsCoroutine()) return ;
    for (;;) Run();
}

void Scheduler::AddTaskRunnable(Task* tk, int dispatch)
{
    DebugPrint(dbg_scheduler, "Add task(%s) to runnable list.", tk->DebugInfo());
    if (tk->proc_)
        tk->proc_->AddTaskRunnable(tk);
    else {
        if (dispatch <= egod_default)
            dispatch = GetOptions().enable_work_steal ? egod_local_thread : egod_robin;

        switch (dispatch) {
            case egod_random:
                {
                    std::size_t n = std::max<std::size_t>(run_proc_list_.size(), 1);
                    GetProcesser(rand() % n)->AddTaskRunnable(tk);
                }
                return ;

            case egod_robin:
                {
                    std::size_t n = std::max<std::size_t>(run_proc_list_.size(), 1);
                    GetProcesser(dispatch_robin_index_++ % n)->AddTaskRunnable(tk);
                }
                return ;

            case egod_local_thread:
                {
                    ThreadLocalInfo &info = GetLocalInfo();
                    if (info.proc)
                        info.proc->AddTaskRunnable(tk);
                    else
                        GetProcesser(0)->AddTaskRunnable(tk);
                }
                return ;
        }

        // 指定了线程索引
        GetProcesser(dispatch)->AddTaskRunnable(tk);
    }
}

uint32_t Scheduler::TaskCount()
{
    return task_count_;
}

uint64_t Scheduler::GetCurrentTaskID()
{
    Task* tk = GetCurrentTask();
    return tk ? tk->id_ : 0;
}

uint64_t Scheduler::GetCurrentTaskYieldCount()
{
    Task* tk = GetCurrentTask();
    return tk ? tk->yield_count_ : 0;
}

void Scheduler::SetCurrentTaskDebugInfo(std::string const& info)
{
    Task* tk = GetCurrentTask();
    if (!tk) return ;
    tk->SetDebugInfo(info);
}

const char* Scheduler::GetCurrentTaskDebugInfo()
{
    Task* tk = GetCurrentTask();
    return tk ? tk->DebugInfo() : "";
}

uint32_t Scheduler::GetCurrentThreadID()
{
    return GetLocalInfo().thread_id;
}

uint32_t Scheduler::GetCurrentProcessID()
{
#if __linux__
    return getpid();
#else
    return 0;
#endif 
}

Task* Scheduler::GetCurrentTask()
{
    ThreadLocalInfo& info = GetLocalInfo();
    return info.proc ? info.proc->GetCurrentTask() : nullptr;
}

void Scheduler::SleepSwitch(int timeout_ms)
{
    if (timeout_ms <= 0)
        CoYield();
    else
        sleep_wait_.CoSwitch(timeout_ms);
}

bool Scheduler::CancelTimer(TimerId timer_id)
{
    bool ok = timer_mgr_.Cancel(timer_id);
    DebugPrint(dbg_timer, "cancel timer %llu %s", (long long unsigned)timer_id->GetId(),
            ok ? "success" : "failed");
    return ok;
}

bool Scheduler::BlockCancelTimer(TimerId timer_id)
{
    bool ok = timer_mgr_.BlockCancel(timer_id);
    DebugPrint(dbg_timer, "block_cancel timer %llu %s", (long long unsigned)timer_id->GetId(),
            ok ? "success" : "failed");
    return ok;
}

ThreadPool& Scheduler::GetThreadPool()
{
    if (!thread_pool_) {
        std::unique_lock<LFLock> lock(thread_pool_init_);
        if (!thread_pool_) {
            thread_pool_ = new ThreadPool;
        }
    }

    return *thread_pool_;
}

uint32_t codebug_GetCurrentProcessID()
{
    return g_Scheduler.GetCurrentProcessID();
}
uint32_t codebug_GetCurrentThreadID()
{
    return g_Scheduler.GetCurrentThreadID();
}
std::string codebug_GetCurrentTime()
{
#if __linux__
    struct tm local;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &local);
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%06lu",
            local.tm_year+1900, local.tm_mon+1, local.tm_mday, 
            local.tm_hour, local.tm_min, local.tm_sec, tv.tv_usec);
    return std::string(buffer);
#else
    return std::string();
#endif
}

} //namespace co
