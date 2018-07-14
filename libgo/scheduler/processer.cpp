#include "processer.h"
#include "scheduler.h"
#include "../common/error.h"
#include "../common/clock.h"
#include <assert.h>
#include "ref.h"

namespace co {

int Processer::s_check_ = 0;

Processer::Processer(int id)
    : id_(id)
{
    runnableQueue_.check_ = (void*)&s_check_;
    waitQueue_.check_ = (void*)&s_check_;
    gcQueue_.check_ = (void*)&s_check_;
    newQueue_.check_ = (void*)&s_check_;
}

Processer* & Processer::GetCurrentProcesser()
{
    static thread_local Processer *proc = nullptr;
    return proc;
}

void Processer::AddTaskRunnable(Task *tk)
{
    DebugPrint(dbg_scheduler, "task(%s) add into proc(%u)", tk->DebugInfo(), id_);
    newQueue_.push(tk);

    if (IsWaiting()) {
        waiting_ = false;
        NotifyCondition();
    }
}

void Processer::AddTaskRunnable(SList<Task> && slist)
{
    DebugPrint(dbg_scheduler, "task(num=%d) add into proc(%u)", (int)slist.size(), id_);
    newQueue_.push(std::move(slist));

    if (IsWaiting()) {
        waiting_ = false;
        NotifyCondition();
    }
}

void Processer::Process()
{
    GetCurrentProcesser() = this;

    ContextScopedGuard guard;
    (void)guard;

    for (;;)
    {
        runnableQueue_.front(runningTask_);

        if (!runningTask_) {
            if (AddNewTasks())
                runnableQueue_.front(runningTask_);

            if (!runningTask_) {
                WaitCondition();
                AddNewTasks();
                continue;
            }
        }

        DebugPrint(dbg_scheduler, "Run [Proc(%d) QueueSize:%lu] --------------------------", id_, RunnableSize());

        addNewQuota_ = 1;
        while (runningTask_) {
            runningTask_->state_ = TaskState::runnable;
            runningTask_->proc_ = this;
            DebugPrint(dbg_switch, "enter task(%s)", runningTask_->DebugInfo());
            if (g_Scheduler.GetTaskListener())
                g_Scheduler.GetTaskListener()->onSwapIn(runningTask_->id_);
            ++switchCount_;
            if (!runningTask_->SwapIn()) {
                fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
                runningTask_ = nullptr;
                runningTask_->DecrementRef();
                ThrowError(eCoErrorCode::ec_swapcontext_failed);
            }
            DebugPrint(dbg_switch, "leave task(%s) state=%d", runningTask_->DebugInfo(), (int)runningTask_->state_);

            switch (runningTask_->state_) {
                case TaskState::runnable:
                    {
                        runnableQueue_.next(runningTask_, runningTask_);
                        if (!runningTask_ && addNewQuota_ > 0) {
                            if (AddNewTasks()) {
                                runnableQueue_.next(runningTask_, runningTask_);
                                -- addNewQuota_;
                            }
                        }
                    }
                    break;

                case TaskState::block:
                    {
                        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
                        runningTask_ = nextTask_;
                        nextTask_ = nullptr;
                    }
                    break;

                case TaskState::done:
                default:
                    {
                        runnableQueue_.next(runningTask_, nextTask_);
                        if (!nextTask_ && addNewQuota_ > 0) {
                            if (AddNewTasks()) {
                                runnableQueue_.next(runningTask_, nextTask_);
                                -- addNewQuota_;
                            }
                        }

                        DebugPrint(dbg_task, "task(%s) done.", runningTask_->DebugInfo());
                        runnableQueue_.erase(runningTask_);
                        if (gcQueue_.size() > 16)
                            GC();
                        gcQueue_.push(runningTask_);
                        if (runningTask_->eptr_) {
                            std::exception_ptr ep = runningTask_->eptr_;
                            std::rethrow_exception(ep);
                        }

                        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
                        runningTask_ = nextTask_;
                        nextTask_ = nullptr;
                    }
                    break;
            }
        }
    }
}

void Processer::CoYield()
{
    Task *tk = GetCurrentTask();
    assert(tk);

    DebugPrint(dbg_yield, "yield task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
    ++ TaskRefYieldCount(tk);

    if (g_Scheduler.GetTaskListener())
        g_Scheduler.GetTaskListener()->onSwapOut(tk->id_);

    if (!tk->SwapOut()) {
        fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
        ThrowError(eCoErrorCode::ec_yield_failed);
    }
}

Task* Processer::GetCurrentTask()
{
    return runningTask_;
}

std::size_t Processer::RunnableSize()
{
    return runnableQueue_.size() + newQueue_.size();
}

void Processer::NotifyCondition()
{
    std::unique_lock<std::mutex> lock(cvMutex_);
    cv_.notify_all();
}

void Processer::WaitCondition()
{
    GC();
    std::unique_lock<std::mutex> lock(cvMutex_);
    waiting_ = true;
    cv_.wait_for(lock, std::chrono::milliseconds(100));
    waiting_ = false;
}

void Processer::GC()
{
    auto list = gcQueue_.pop_all();
    for (Task & tk : list)
        tk.DecrementRef();
    list.clear();
}

bool Processer::AddNewTasks()
{
    if (newQueue_.emptyUnsafe()) return false;

    runnableQueue_.push(newQueue_.pop_all());
    return true;
}

bool Processer::IsBlocking()
{
    if (!markSwitch_ || markSwitch_ != switchCount_) return false;
    return NowMicrosecond() > markTick_ + CoroutineOptions::getInstance().cycle_timeout_us;
}

void Processer::Mark()
{
    if (runningTask_ && markSwitch_ != switchCount_) {
        markSwitch_ = switchCount_;
        markTick_ = NowMicrosecond();
    }
}

int64_t Processer::NowMicrosecond()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(FastSteadyClock::now().time_since_epoch()).count();
}

SList<Task> Processer::Steal(std::size_t n)
{
    if (n > 0) {
        // steal some
        auto slist = newQueue_.pop_back(n);
        if (slist.size() >= n)
            return slist;

        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
        if (runningTask_)
            runnableQueue_.eraseWithoutLock(runningTask_);
        if (nextTask_)
            runnableQueue_.eraseWithoutLock(nextTask_);
        auto slist2 = runnableQueue_.pop_backWithoutLock(n - slist.size());
        if (runningTask_)
            runnableQueue_.pushWithoutLock(runningTask_);
        if (nextTask_)
            runnableQueue_.pushWithoutLock(nextTask_);
        lock.unlock();

        slist2.append(std::move(slist));
        if (!slist2.empty())
            DebugPrint(dbg_scheduler, "Proc(%d).Stealed = %d", id_, (int)slist2.size());
        return slist2;
    } else {
        // steal all
        auto slist = newQueue_.pop_all();

        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
        if (runningTask_)
            runnableQueue_.eraseWithoutLock(runningTask_);
        if (nextTask_)
            runnableQueue_.eraseWithoutLock(nextTask_);
        auto slist2 = runnableQueue_.pop_allWithoutLock();
        if (runningTask_)
            runnableQueue_.pushWithoutLock(runningTask_);
        if (nextTask_)
            runnableQueue_.pushWithoutLock(nextTask_);
        lock.unlock();

        slist2.append(std::move(slist));
        if (!slist2.empty())
            DebugPrint(dbg_scheduler, "Proc(%d).Stealed all = %d", id_, (int)slist2.size());
        return slist2;
    }
}

Processer::SuspendEntry Processer::Suspend()
{
    Task* tk = g_Scheduler.GetCurrentTask();
    assert(tk);
    assert(tk->proc_);
    return tk->proc_->SuspendBySelf(tk);
}

Processer::SuspendEntry Processer::Suspend(FastSteadyClock::duration dur)
{
    SuspendEntry entry = Suspend();
    g_Scheduler.GetTimer().StartTimer(dur,
            [entry]() mutable {
                Processer::Wakeup(entry);
            });
    return entry;
}

Processer::SuspendEntry Processer::SuspendBySelf(Task* tk)
{
    assert(tk == runningTask_);
    assert(tk->state_ == TaskState::runnable);

    tk->state_ = TaskState::block;
    runnableQueue_.next(runningTask_, nextTask_);
    if (!nextTask_ && addNewQuota_ > 0) {
        if (AddNewTasks()) {
            runnableQueue_.next(runningTask_, nextTask_);
            -- addNewQuota_;
        }
    }
    uint64_t id = ++ TaskRefSuspendId(tk);
    runnableQueue_.erase(runningTask_);
    waitQueue_.push(runningTask_);
    return SuspendEntry{ IncursivePtr<Task>(tk), id };
}

bool Processer::Wakeup(SuspendEntry & entry)
{
    auto proc = entry.tk_->proc_;
    if (!proc) {
        entry.tk_.reset();
        return false;
    }
    return proc->WakeupBySelf(entry);
}

bool Processer::WakeupBySelf(SuspendEntry & entry)
{
    IncursivePtr<Task> tkPtr;
    tkPtr.swap(entry.tk_);
    Task* tk = tkPtr.get();

    if (entry.id_ != TaskRefSuspendId(tk)) return false;

    {
        std::unique_lock<TaskQueue::lock_t> lock(waitQueue_.LockRef());
        if (entry.id_ != TaskRefSuspendId(tk)) return false;
        ++ TaskRefSuspendId(tk);
        bool ret = waitQueue_.eraseWithoutLock(tk);
        assert(ret);
    }

    AddTaskRunnable(tk);
    return true;
}

} //namespace co

