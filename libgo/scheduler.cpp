#include "scheduler.h"
#include "error.h"
#include <stdio.h>
#include <system_error>
#include <unistd.h>
#include "thread_pool.h"

namespace co
{

Scheduler& Scheduler::getInstance()
{
    static Scheduler obj;
    return obj;
}

extern void coroutine_hook_init();
Scheduler::Scheduler()
{
    thread_pool_ = new ThreadPool;
    coroutine_hook_init();
}

Scheduler::~Scheduler()
{
    delete thread_pool_;
}

ThreadLocalInfo& Scheduler::GetLocalInfo()
{
    static co_thread_local ThreadLocalInfo *info = NULL;
    if (!info)
        info = new ThreadLocalInfo();

    return *info;
}

CoroutineOptions& Scheduler::GetOptions()
{
    static CoroutineOptions options;
    return options;
}

void Scheduler::CreateTask(TaskF const& fn, std::size_t stack_size,
        const char* file, int lineno)
{
    Task* tk = new Task(fn, stack_size ? stack_size : GetOptions().stack_size);
    tk->InitLocation(file, lineno);
    ++task_count_;
    DebugPrint(dbg_task, "task(%s) created.", tk->DebugInfo());
    AddTaskRunnable(tk);
}

bool Scheduler::IsCoroutine()
{
    return !!GetLocalInfo().current_task;
}

bool Scheduler::IsEmpty()
{
    return task_count_ == 0;
}

void Scheduler::CoYield()
{
    Task* tk = GetLocalInfo().current_task;
    if (!tk) return ;
    tk->proc_->CoYield(GetLocalInfo());
}

uint32_t Scheduler::Run()
{
    ThreadLocalInfo &info = GetLocalInfo();
    if (!info.thread_id) {
        info.thread_id = ++ thread_id_;
    }

    // 创建、增补P
    CoroutineOptions &op = GetOptions();
    if (proc_count < op.processer_count) {
        std::unique_lock<LFLock> lock(proc_init_lock_, std::defer_lock);
        if (lock.try_lock() && proc_count < op.processer_count) {
            uint32_t i = proc_count;
            for (; i < op.processer_count; ++i) {
                Processer* proc = new Processer(op.stack_size);
                run_proc_list_.push(proc);
            }

            proc_count = i;
        }
    }

    uint32_t run_task_count = DoRunnable();

    // timer
    uint32_t tm_count = DoTimer();

    // sleep wait.
    uint32_t sl_count = DoSleep();

    // epoll
    bool enable_block = !run_task_count && !tm_count && !sl_count;
    int ep_count = DoEpoll(enable_block);

    if (!run_task_count && ep_count <= 0 && !tm_count && !sl_count) {
        if (ep_count == -1) {
            // 此线程没有执行epoll_wait, 使用sleep降低空转时的cpu使用率
            DebugPrint(dbg_scheduler_sleep, "sleep %d ms", (int)sleep_ms_);
            sleep_ms_ = (std::min)(++sleep_ms_, GetOptions().max_sleep_ms);
            usleep(sleep_ms_ * 1000);
        } else {
            // 此线程执行了epoll_wait, 增加epoll_wait超时时间降低空转时的cpu使用率
            io_wait_.DelayEventWaitTime();
        }
    } else {
        sleep_ms_ = 1;
        io_wait_.ResetEventWaitTime();
    }

    return run_task_count;
}

void Scheduler::RunUntilNoTask()
{
    do { 
        Run();
    } while (!IsEmpty());
}

// Run函数的一部分, 处理runnable状态的协程
uint32_t Scheduler::DoRunnable()
{
    uint32_t do_count = 0;
    uint32_t proc_c = run_proc_list_.size();
    for (uint32_t i = 0; i < proc_c; ++i)
    {
        Processer *proc = run_proc_list_.pop();
        if (!proc) break;

        // cherry-pick tasks.
        if (!run_tasks_.empty()) {
            uint32_t task_c = task_count_;
            uint32_t average = task_c / proc_c + (task_c % proc_c ? 1 : 0);

            uint32_t ti = proc->GetTaskCount();
            uint32_t popn = average > ti ? (average - ti) : 0;
            if (popn) {
                static int sc = 0;
                SList<Task> s(run_tasks_.pop(popn));
                auto it = s.begin();
                while (it != s.end()) {
                    Task* tk = &*it;
                    it = s.erase(it);
                    proc->AddTaskRunnable(tk);
                }
                sc += s.size();
//                printf("popn = %d, get %d coroutines, sc=%d, remain=%d\n",
//                        (int)popn, (int)s.size(), (int)sc, (int)run_tasks_.size());
            }
//            for (uint32_t ti = proc->GetTaskCount(); ti < average; ++ti) {
//                Task *tk = run_tasks_.pop();
//                if (!tk) break;
//                proc->AddTaskRunnable(tk);
//            }
        }

        uint32_t done_count = 0;
        try {
            do_count += proc->Run(GetLocalInfo(), done_count);
        } catch (...) {
            task_count_ -= done_count;
            run_proc_list_.push(proc);
            throw ;
        }
        task_count_ -= done_count;
//        printf("run %d coroutines, remain=%d\n", (int)done_count, (int)task_count_);

        run_proc_list_.push(proc);
    }

    return do_count;
}

// Run函数的一部分, 处理epoll相关
int Scheduler::DoEpoll(bool enable_block)
{
    return io_wait_.WaitLoop(enable_block);
}

uint32_t Scheduler::DoSleep()
{
    return sleep_wait_.WaitLoop();
}

// Run函数的一部分, 处理定时器
uint32_t Scheduler::DoTimer()
{
    uint32_t c = 0;
    while (!GetOptions().timer_handle_every_cycle || c < GetOptions().timer_handle_every_cycle)
    {
        std::list<CoTimerPtr> timers;
        timer_mgr_.GetExpired(timers, 128);
        for (auto &sp_timer : timers)
        {
            DebugPrint(dbg_timer, "enter timer callback %llu", (long long unsigned)sp_timer->GetId());
            (*sp_timer)();
            DebugPrint(dbg_timer, "leave timer callback %llu", (long long unsigned)sp_timer->GetId());
        }
        c += timers.size();
        if (timers.empty()) break;
    }

    return c;
}

void Scheduler::RunLoop()
{
    for (;;) Run();
}

void Scheduler::AddTaskRunnable(Task* tk)
{
    DebugPrint(dbg_scheduler, "Add task(%s) to runnable list.", tk->DebugInfo());
    if (tk->proc_)
        tk->proc_->AddTaskRunnable(tk);
    else
        run_tasks_.push(tk);
}

uint32_t Scheduler::TaskCount()
{
    return task_count_;
}

uint64_t Scheduler::GetCurrentTaskID()
{
    Task* tk = GetLocalInfo().current_task;
    return tk ? tk->id_ : 0;
}

uint64_t Scheduler::GetCurrentTaskYieldCount()
{
    Task* tk = GetLocalInfo().current_task;
    return tk ? tk->yield_count_ : 0;
}

void Scheduler::SetCurrentTaskDebugInfo(std::string const& info)
{
    Task* tk = GetLocalInfo().current_task;
    if (!tk) return ;
    tk->SetDebugInfo(info);
}

const char* Scheduler::GetCurrentTaskDebugInfo()
{
    Task* tk = GetLocalInfo().current_task;
    return tk ? tk->DebugInfo() : "";
}

uint32_t Scheduler::GetCurrentThreadID()
{
    return GetLocalInfo().thread_id;
}

uint32_t Scheduler::GetCurrentProcessID()
{
#ifndef _WIN32
    return getpid();
#else
    return 0;
#endif 
}

Task* Scheduler::GetCurrentTask()
{
    return GetLocalInfo().current_task;
}

void Scheduler::SleepSwitch(int timeout_ms)
{
    if (timeout_ms <= 0)
        CoYield();
    else
        sleep_wait_.CoSwitch(timeout_ms);
}

TimerId Scheduler::ExpireAt(CoTimerMgr::TimePoint const& time_point,
        CoTimer::fn_t const& fn)
{
    TimerId id = timer_mgr_.ExpireAt(time_point, fn);
    DebugPrint(dbg_timer, "add timer %llu", (long long unsigned)id->GetId());
    return id;
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
    return *thread_pool_;
}

uint64_t codebug_GetDebugOptions()
{
    return g_Scheduler.GetOptions().debug;
}
FILE* codebug_GetDebugOutput()
{
    return g_Scheduler.GetOptions().debug_output;
}
uint32_t codebug_GetCurrentProcessID()
{
    return g_Scheduler.GetCurrentProcessID();
}
uint32_t codebug_GetCurrentThreadID()
{
    return g_Scheduler.GetCurrentThreadID();
}

} //namespace co
