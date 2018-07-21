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
    newQueue_.AssertLink();

    if (IsWaiting()) {
        waiting_ = false;
        NotifyCondition();
    }
}

void Processer::AddTaskRunnable(SList<Task> && slist)
{
    DebugPrint(dbg_scheduler, "task(num=%d) add into proc(%u)", (int)slist.size(), id_);
    newQueue_.push(std::move(slist));
    newQueue_.AssertLink();

    if (IsWaiting()) {
        waiting_ = false;
        NotifyCondition();
    }
}

void Processer::Process()
{
    GetCurrentProcesser() = this;

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

        runningTask_->SwapIn();
    }
}

void Processer::RingNext()
{
    std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
    runnableQueue_.nextWithoutLock(runningTask_, nextTask_);
    if (nextTask_) return ;

    if (addNewQuota_ > 0) {
        if (AddNewTasks(false)) {
            runnableQueue_.nextWithoutLock(runningTask_, nextTask_);
        }

        --addNewQuota_;
        if (nextTask_) return ;
    }

    // ring to head
    Task* front = runnableQueue_.frontWithoutLock();
    if (front != runningTask_)
        nextTask_ = front;

    addNewQuota_ = 1;
}

void Processer::CoYield()
{
    Task *tk = GetCurrentTask();
    assert(tk);
    assert(tk == runningTask_);

    DebugPrint(dbg_yield, "leave task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
    ++ TaskRefYieldCount(tk);

    switch (tk->state_) {
        case TaskState::runnable:
            RingNext();
            break;

        case TaskState::block:
            break;

        case TaskState::done:
        default:
            {
                RingNext();

                DebugPrint(dbg_task, "task(%s) done.", tk->DebugInfo());
                runnableQueue_.erase(tk);
                if (gcQueue_.size() > 16)
                    GC();
                gcQueue_.push(tk);
                if (tk->eptr_) {
                    std::exception_ptr ep = tk->eptr_;
                    std::rethrow_exception(ep);
                }
            }
            break;
    }

    if (g_Scheduler.GetTaskListener())
        g_Scheduler.GetTaskListener()->onSwapOut(tk->id_);

    // 切出
    bool bSwitch = true;
    if (nextTask_) {
        tk->SwapTo(nextTask_);
    } else {
        if (tk->state_ != TaskState::runnable)
            tk->SwapOut();
        else {
            addNewQuota_ = 1;
            bSwitch = false;
        }
    }

    // 切入
    OnSwapIn(tk, bSwitch);
}

void Processer::OnSwapIn(Task *tk, bool bSwitch)
{
    tk->state_ = TaskState::runnable;
    tk->proc_ = this;

    if (bSwitch)
    {
        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
        runningTask_ = tk;
        nextTask_ = nullptr;
    }

    DebugPrint(dbg_switch, "enter task(%s)", tk->DebugInfo());
    ++switchCount_;


    if (g_Scheduler.GetTaskListener())
        g_Scheduler.GetTaskListener()->onSwapIn(tk->id_);
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
    for (Task & tk : list) {
        tk.DecrementRef();
    }
    list.clear();
}

bool Processer::AddNewTasks(bool lock)
{
    if (newQueue_.emptyUnsafe()) return false;

    if (lock)
        runnableQueue_.push(newQueue_.pop_all());
    else
        runnableQueue_.pushWithoutLock(newQueue_.pop_all());
    newQueue_.AssertLink();
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
        newQueue_.AssertLink();
        auto slist = newQueue_.pop_back(n);
        newQueue_.AssertLink();
        if (slist.size() >= n)
            return slist;

        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
        bool pushRunningTask = false, pushNextTask = false;
        if (runningTask_)
            pushRunningTask = runnableQueue_.eraseWithoutLock(runningTask_, true) || slist.erase(runningTask_, newQueue_.check_);
        if (nextTask_)
            pushNextTask = runnableQueue_.eraseWithoutLock(nextTask_, true) || slist.erase(nextTask_, newQueue_.check_);
        auto slist2 = runnableQueue_.pop_backWithoutLock(n - slist.size());
        if (pushRunningTask)
            runnableQueue_.pushWithoutLock(runningTask_);
        if (pushNextTask)
            runnableQueue_.pushWithoutLock(nextTask_);
        lock.unlock();

        slist2.append(std::move(slist));
        if (!slist2.empty())
            DebugPrint(dbg_scheduler, "Proc(%d).Stealed = %d", id_, (int)slist2.size());
        return slist2;
    } else {
        // steal all
        newQueue_.AssertLink();
        auto slist = newQueue_.pop_all();
        newQueue_.AssertLink();

        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
        bool pushRunningTask = false, pushNextTask = false;
        if (runningTask_)
            pushRunningTask = runnableQueue_.eraseWithoutLock(runningTask_, true) || slist.erase(runningTask_, newQueue_.check_);
        if (nextTask_)
            pushNextTask = runnableQueue_.eraseWithoutLock(nextTask_, true) || slist.erase(nextTask_, newQueue_.check_);
        auto slist2 = runnableQueue_.pop_allWithoutLock();
        if (pushRunningTask)
            runnableQueue_.pushWithoutLock(runningTask_);
        if (pushNextTask)
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
    uint64_t id = ++ TaskRefSuspendId(tk);

    RingNext();
    runnableQueue_.erase(runningTask_);
    waitQueue_.push(runningTask_);
    return SuspendEntry{ WeakPtr<Task>(tk), id };
}

bool Processer::Wakeup(SuspendEntry const& entry)
{
    IncursivePtr<Task> tkPtr = entry.tk_.lock();
    if (!tkPtr) return false;

    auto proc = tkPtr->proc_;
    return proc ? proc->WakeupBySelf(tkPtr, entry.id_) : false;
}

bool Processer::WakeupBySelf(IncursivePtr<Task> const& tkPtr, uint64_t id)
{
    Task* tk = tkPtr.get();

    if (id != TaskRefSuspendId(tk)) return false;

    {
        std::unique_lock<TaskQueue::lock_t> lock(waitQueue_.LockRef());
        if (id != TaskRefSuspendId(tk)) return false;
        ++ TaskRefSuspendId(tk);
        bool ret = waitQueue_.eraseWithoutLock(tk, true);
        assert(ret);
    }

    AddTaskRunnable(tk);
    return true;
}

} //namespace co

