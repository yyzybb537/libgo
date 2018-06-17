#include "scheduler.h"
#include "../common/error.h"
#include <stdio.h>
#include <system_error>
#include <unistd.h>
#include <time.h>
#include "ref.h"
#include <thread>
#include <sys/sysinfo.h>
#if __linux__
#include <sys/time.h>
#endif
#if WITH_SAFE_SIGNAL
#include "hook_signal.h"
#endif

namespace co
{

extern void coroutine_hook_init();

inline atomic_t<unsigned long long> & GetTaskIdFactory()
{
    static atomic_t<unsigned long long> factory;
    return factory;
}

Scheduler::Scheduler()
{
//    coroutine_hook_init();

    // register TaskAnys.
    TaskRefInit(Affinity);
    TaskRefInit(YieldCount);
    TaskRefInit(Location);
    TaskRefInit(DebugInfo);

    processers_.push_back(new Processer(0));
}

Scheduler::~Scheduler()
{
}

Processer* Scheduler::GetProcesser(std::size_t index)
{
    if (processers_.size() > index)
        return processers_[index];

    return processers_[0];
}

void Scheduler::CreateTask(TaskF const& fn, TaskOpt const& opt)
{
    Task* tk = new Task(fn, opt.stack_size_ ? opt.stack_size_ : GetOptions().stack_size);
    tk->id_ = ++GetTaskIdFactory();
    TaskRefAffinity(tk) = opt.affinity_;
    TaskRefLocation(tk).Init(opt.file_, opt.lineno_);
    ++task_count_;

    DebugPrint(dbg_task, "task(%s) created.", TaskDebugInfo(tk));
    if (GetTaskListener()) {
        GetTaskListener()->onCreated(tk->id_);
    }

    AddTaskRunnable(tk, opt.dispatch_);
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
    auto proc = Processer::GetCurrentProcesser();
    if (proc)
        proc->CoYield();
}

void Scheduler::Start(int minThreadNumber, int maxThreadNumber)
{
    if (!started_.try_lock())
        throw std::logic_error("libgo repeated call Scheduler::Start");

    if (minThreadNumber < 1)
       minThreadNumber = get_nprocs();

    if (maxThreadNumber == 0 || maxThreadNumber < minThreadNumber)
        maxThreadNumber = minThreadNumber;

    minThreadNumber_ = minThreadNumber;
    maxThreadNumber_ = maxThreadNumber;

#ifndef LIBGO_SINGLE_THREAD
    for (int i = 0; i < minThreadNumber_ - 1; i++) {
        auto p = new Processer(i+1);
        std::thread t([p]{
                p.Process();
            });
        p.BindThread(t);
        this->processers_.push_back(p);
    }

    if (maxThreadNumber_ > 1) {
        std::thread t([this]{
                this->DispatcherThread();
                });
        t.detach();
    }
#endif

    GetProcesser(0)->Process();
}

void Scheduler::DispatcherThread()
{
    for (;;) {
        // TODO: 用condition_variable降低cpu使用率
        usleep(100);

        // wakeup waiting process
        for (std::size_t i = 0; i < processers_.size(); i++) {
            auto p = processers_[i];
            if (p->IsWaiting()) {
                if (p->RunnableSize() > 0)
                    p->NotifyCondition();
                else {
                    // 空闲线程
                }
            }
        }

    }
}

//void Scheduler::Process()
//{
//    ThreadLocalInfo &info = GetLocalInfo();
//    if (info.thread_id < 0) {
//        info.thread_id = thread_id_++;
//        info.proc = GetProcesser(info.thread_id);
//
//        if (info.thread_id > 0) {
//            // 超过一个调度线程
//        }
//    }
//
//    if (IsCoroutine()) return 0;
//
//#if LIBGO_SINGLE_THREAD
//    if (info.thread_id > 0) {
//        usleep(20 * 1000);
//        return 0;
//    }
//#endif
//
//    DoRunnable();
//
//    // timer
//    long long timer_next_ms = GetOptions().max_sleep_ms;
//    uint32_t tm_count = 0;
//    if (flags & erf_do_timer)
//        tm_count = DoTimer(timer_next_ms);
//
//    // sleep wait.
//    long long sleep_next_ms = GetOptions().max_sleep_ms;
//    uint32_t sl_count = 0;
//    if (flags & erf_do_sleeper)
//        sl_count = DoSleep(sleep_next_ms);
//
//    // 下一次timer或sleeper触发的时间毫秒数, 休眠或阻塞等待IO事件触发的时间不能超过这个值
//    long long next_ms = (std::min)(timer_next_ms, sleep_next_ms);
//
//    // epoll
//    int ep_count = -1;
//    if (flags & erf_do_eventloop) {
//        int wait_milliseconds = 0;
//        if (run_task_count || tm_count || sl_count)
//            wait_milliseconds = 0;
//        else {
//            wait_milliseconds = (std::min<long long>)(next_ms, GetOptions().max_sleep_ms);
//            DebugPrint(dbg_scheduler_sleep, "wait_milliseconds %d ms, next_ms=%lld", wait_milliseconds, next_ms);
//        }
//        ep_count = DoEpoll(wait_milliseconds);
//    }
//
//    if (flags & erf_idle_cpu) {
//        if (!run_task_count && ep_count <= 0 && !tm_count && !sl_count) {
//            if (ep_count == -1) {
//                // 此线程没有执行epoll_wait, 使用sleep降低空转时的cpu使用率
//                ++info.sleep_ms;
//                info.sleep_ms = (std::min)(info.sleep_ms, GetOptions().max_sleep_ms);
//                info.sleep_ms = (std::min<long long>)(info.sleep_ms, next_ms);
//                DebugPrint(dbg_scheduler_sleep, "sleep %d ms, next_ms=%lld", (int)info.sleep_ms, next_ms);
//                usleep(info.sleep_ms * 1000);
//            }
//        } else {
//            info.sleep_ms = 1;
//        }
//    }
//
//#if WITH_SAFE_SIGNAL
//    if (flags & erf_signal) {
//        HookSignal::getInstance().Run();
//    }
//#endif
//
//    return run_task_count;
//}

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
                    std::size_t n = std::max<std::size_t>(processers_.size(), 1);
                    GetProcesser(rand() % n)->AddTaskRunnable(tk);
                }
                return ;

            case egod_robin:
                {
                    std::size_t n = std::max<std::size_t>(processers_.size(), 1);
                    GetProcesser(dispatch_robin_index_++ % n)->AddTaskRunnable(tk);
                }
                return ;

            case egod_local_thread:
                {
                    auto proc = Processer::GetCurrentProcesser();
                    if (proc)
                        proc->AddTaskRunnable(tk);
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
    return tk ? TaskRefYieldCount(tk) : 0;
}

void Scheduler::SetCurrentTaskDebugInfo(std::string const& info)
{
    Task* tk = GetCurrentTask();
    if (!tk) return ;
    TaskRefDebugInfo(tk) = info;
}

const char* Scheduler::GetCurrentTaskDebugInfo()
{
    Task* tk = GetCurrentTask();
    return tk ? tk->DebugInfo() : "";
}

int Scheduler::GetCurrentThreadID()
{
    auto proc = Processer::GetCurrentProcesser();
    return proc ? proc->Id() : -1;
}

int Scheduler::GetCurrentProcessID()
{
#if __linux__
    return getpid();
#else
    return 0;
#endif 
}

Task* Scheduler::GetCurrentTask()
{
    auto proc = Processer::GetCurrentProcesser();
    return proc ? proc->GetCurrentTask() : nullptr;
}

//bool Scheduler::CancelTimer(TimerId timer_id)
//{
//    bool ok = timer_mgr_.Cancel(timer_id);
//    DebugPrint(dbg_timer, "cancel timer %llu %s", (long long unsigned)timer_id->GetId(),
//            ok ? "success" : "failed");
//    return ok;
//}
//
//bool Scheduler::BlockCancelTimer(TimerId timer_id)
//{
//    bool ok = timer_mgr_.BlockCancel(timer_id);
//    DebugPrint(dbg_timer, "block_cancel timer %llu %s", (long long unsigned)timer_id->GetId(),
//            ok ? "success" : "failed");
//    return ok;
//}
//
//ThreadPool& Scheduler::GetThreadPool()
//{
//    if (!thread_pool_) {
//        std::unique_lock<LFLock> lock(thread_pool_init_);
//        if (!thread_pool_) {
//            thread_pool_ = new ThreadPool;
//        }
//    }
//
//    return *thread_pool_;
//}

int codebug_GetCurrentProcessID()
{
    return g_Scheduler.GetCurrentProcessID();
}
int codebug_GetCurrentThreadID()
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
