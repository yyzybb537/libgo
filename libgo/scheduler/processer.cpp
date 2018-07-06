#include "processer.h"
#include "scheduler.h"
#include "../common/error.h"
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
    tk->state_ = TaskState::runnable;
    tk->proc_ = this;
    newQueue_.push(tk);

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
        DebugPrint(dbg_scheduler, "Run [Proc(%d) QueueSize:%lu] --------------------------", id_, RunnableSize());

        runnableQueue_.front(runningTask_);

        if (!runningTask_) {
            if (AddNewTasks())
                runnableQueue_.front(runningTask_);
        }

        if (!runningTask_) {
            WaitCondition();
            AddNewTasks();
            continue;
        }

        while (runningTask_) {

            DebugPrint(dbg_switch, "enter task(%s)", runningTask_->DebugInfo());
            if (g_Scheduler.GetTaskListener())
                g_Scheduler.GetTaskListener()->onSwapIn(runningTask_->id_);
            UpdateTick();
            if (!runningTask_->SwapIn()) {
                fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
                runningTask_ = nullptr;
                runningTask_->DecrementRef();
                ThrowError(eCoErrorCode::ec_swapcontext_failed);
            }
            runTick_ = 0;
            DebugPrint(dbg_switch, "leave task(%s) state=%d", runningTask_->DebugInfo(), (int)runningTask_->state_);

            runnableQueue_.next(runningTask_, nextTask_);
            if (!nextTask_) {
                if (AddNewTasks())
                    runnableQueue_.next(runningTask_, nextTask_);
            }

            switch (runningTask_->state_) {
                case TaskState::runnable:
                    break;

                case TaskState::block:
                    runnableQueue_.erase(runningTask_);
                    waitQueue_.push(runningTask_);
                    break;

                case TaskState::done:
                default:
                    DebugPrint(dbg_task, "task(%s) done.", runningTask_->DebugInfo());
                    runnableQueue_.erase(runningTask_);
                    if (gcQueue_.size() > 16)
                        GC();
                    gcQueue_.push(runningTask_);
                    if (runningTask_->eptr_) {
                        std::exception_ptr ep = runningTask_->eptr_;
                        std::rethrow_exception(ep);
                    }
                    break;
            }

//            std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
            runningTask_ = nextTask_;
            nextTask_ = nullptr;
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
}

bool Processer::AddNewTasks()
{
    if (newQueue_.empty()) return false;

    runnableQueue_.push(newQueue_.pop_all());
    return true;
}

bool Processer::IsTimeout()
{
    return NowMicrosecond() - runTick_ > CoroutineOptions::getInstance().cycle_timeout_us;
}

void Processer::UpdateTick()
{
    runTick_ = NowMicrosecond();
}

int64_t Processer::NowMicrosecond()
{

}

} //namespace co
