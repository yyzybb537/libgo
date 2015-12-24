#include "scheduler.h"
#include <ucontext.h>
#include "error.h"
#include <stdio.h>
#include <system_error>
#include <unistd.h>
#include "thread_pool.h"
#include "platform_adapter.h"

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

void Scheduler::CreateTask(TaskF const& fn)
{
    Task* tk = new Task(fn);
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
                Processer *proc = new Processer(op.stack_size);
                run_proc_list_.push(proc);
            }

            proc_count = i;
        }
    }

    uint32_t run_task_count = DoRunnable();

    // epoll
    int ep_count = DoEpoll();

    // timer
    uint32_t tm_count = DoTimer();

    // sleep wait.
    uint32_t sl_count = DoSleep();

    if (!run_task_count && ep_count <= 0 && !tm_count && !sl_count) {
        DebugPrint(dbg_scheduler_sleep, "sleep %d ms", (int)sleep_ms_);
        sleep_ms_ = (std::min)(++sleep_ms_, GetOptions().max_sleep_ms);
        usleep(sleep_ms_ * 1000);
    } else {
        sleep_ms_ = 1;
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
            for (uint32_t ti = proc->GetTaskCount(); ti < average; ++ti) {
                Task *tk = run_tasks_.pop();
                if (!tk) break;
                proc->AddTaskRunnable(tk);
            }
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

        run_proc_list_.push(proc);
    }

    return do_count;
}

// Run函数的一部分, 处理epoll相关
int Scheduler::DoEpoll()
{
    return io_wait_.WaitLoop();
}

uint32_t Scheduler::DoSleep()
{
    return sleep_wait_.WaitLoop();
}

// Run函数的一部分, 处理定时器
uint32_t Scheduler::DoTimer()
{
    std::list<CoTimerPtr> timers;
    timer_mgr_.GetExpired(timers, 128);
    for (auto &sp_timer : timers)
    {
        DebugPrint(dbg_timer, "enter timer callback %llu", (long long unsigned)sp_timer->GetId());
        (*sp_timer)();
        DebugPrint(dbg_timer, "leave timer callback %llu", (long long unsigned)sp_timer->GetId());
    }

    return timers.size();
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

Task* Scheduler::GetCurrentTask()
{
    return GetLocalInfo().current_task;
}

void Scheduler::IOBlockSwitch(int fd, uint32_t event, int timeout_ms)
{
    std::vector<FdStruct> fdst(1);
    fdst[0].fd = fd;
    fdst[0].event = event;
    IOBlockSwitch(std::move(fdst), timeout_ms);
}

void Scheduler::IOBlockSwitch(std::vector<FdStruct> && fdsts, int timeout_ms)
{
    io_wait_.CoSwitch(std::move(fdsts), timeout_ms);
}

void Scheduler::SleepSwitch(int timeout_ms)
{
    if (timeout_ms <= 0)
        CoYield();
    else
        sleep_wait_.CoSwitch(timeout_ms);
}

bool Scheduler::UserBlockWait(uint32_t type, uint64_t wait_id)
{
    return BlockWait((int64_t)type, wait_id);
}

bool Scheduler::TryUserBlockWait(uint32_t type, uint64_t wait_id)
{
    return TryBlockWait((int64_t)type, wait_id);
}

uint32_t Scheduler::UserBlockWakeup(uint32_t type, uint64_t wait_id, uint32_t wakeup_count)
{
    return BlockWakeup((int64_t)type, wait_id, wakeup_count);
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

bool Scheduler::BlockWait(int64_t type, uint64_t wait_id)
{
    if (!IsCoroutine()) return false;
    Task* tk = GetLocalInfo().current_task;
    tk->user_wait_type_ = type;
    tk->user_wait_id_ = wait_id;
    tk->state_ = type < 0 ? TaskState::sys_block : TaskState::user_block;
    DebugPrint(dbg_wait, "task(%s) %s. wait_type=%lld, wait_id=%llu",
            tk->DebugInfo(), type < 0 ? "sys_block" : "user_block",
            (long long int)tk->user_wait_type_, (long long unsigned)tk->user_wait_id_);
    CoYield();
    return true;
}

bool Scheduler::TryBlockWait(int64_t type, uint64_t wait_id)
{
    std::unique_lock<LFLock> locker(user_wait_lock_);
    auto it = user_wait_tasks_.find(type);
    if (user_wait_tasks_.end() == it) return false;

    auto &zone = it->second;
    auto it2 = zone.find(wait_id);
    if (zone.end() == it2) return false;

    auto &wait_pair = it2->second;
    if (wait_pair.first > 0) {
        --wait_pair.first;
        ClearWaitPairWithoutLock(type, wait_id, zone, wait_pair);
        return true;
    }

    return false;
}

uint32_t Scheduler::BlockWakeup(int64_t type, uint64_t wait_id, uint32_t wakeup_count)
{
    std::unique_lock<LFLock> locker(user_wait_lock_);
    auto &zone = user_wait_tasks_[type];
    auto &wait_pair = zone[wait_id];
    auto &task_queue = wait_pair.second;
    SList<Task> tasks = task_queue.pop(wakeup_count);
    std::size_t c = tasks.size();
    if (c < wakeup_count) // 允许提前设置唤醒标志, 以便多线程同步。
        wait_pair.first += wakeup_count - c;
    ClearWaitPairWithoutLock(type, wait_id, zone, wait_pair);
    uint32_t domain_wakeup = wait_pair.first;
    locker.unlock();

    for (auto &task: tasks)
    {
        ++c;
        Task *tk = &task;
        DebugPrint(dbg_wait, "%s wakeup task(%s). wait_type=%lld, wait_id=%llu",
                type < 0 ? "sys_block" : "user_block", tk->DebugInfo(), (long long int)type, (long long unsigned)wait_id);
        AddTaskRunnable(tk);
    }

    DebugPrint(dbg_wait, "%s wakeup %u tasks, domain wakeup=%u. wait_type=%lld, wait_id=%llu",
            type < 0 ? "sys_block" : "user_block", (unsigned)c, domain_wakeup, (long long int)type, (long long unsigned)wait_id);
    return c;
}

void Scheduler::ClearWaitPairWithoutLock(int64_t type,
        uint64_t wait_id, WaitZone& zone, WaitPair& wait_pair)
{
    if (wait_pair.second.empty() && wait_pair.first == 0) {
        if (zone.size() > 1) {
            zone.erase(wait_id);
        } else {
            user_wait_tasks_.erase(type);
        }
    }
}

} //namespace co
