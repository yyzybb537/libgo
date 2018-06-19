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

        Task *tk = runnableQueue_.front();
        if (!tk) {
            AddNewTasks();
            tk = runnableQueue_.front();
        }

        if (!tk) {
            WaitCondition();
            AddNewTasks();
            continue;
        }

        while (tk) {
            runningTask_ = tk;
            UpdateTick();

            DebugPrint(dbg_switch, "enter task(%s)", tk->DebugInfo());
            if (g_Scheduler.GetTaskListener())
                g_Scheduler.GetTaskListener()->onSwapIn(tk->id_);
            if (!tk->SwapIn()) {
                fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
                runningTask_ = nullptr;
                tk->DecrementRef();
                ThrowError(eCoErrorCode::ec_swapcontext_failed);
            }
            DebugPrint(dbg_switch, "leave task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
            runningTask_ = nullptr;

            if (!tk->next) {
                AddNewTasks();
            }

            Task *next = (Task*)tk->next;
            switch (tk->state_) {
                case TaskState::runnable:
                    break;

                case TaskState::block:
                    runnableQueue_.erase(tk);
                    waitQueue_.push(tk);
                    break;

                case TaskState::done:
                default:
                    DebugPrint(dbg_task, "task(%s) done.", tk->DebugInfo());
                    runnableQueue_.erase(tk);
                    if (gcQueue_.size() > 16)
                        GC();
                    gcQueue_.push(tk);
                    if (tk->eptr_) {
                        std::exception_ptr ep = tk->eptr_;
                        std::rethrow_exception(ep);
                    }
                    break;
            }
            tk = next;
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

void Processer::AddNewTasks()
{
    if (!newQueue_.empty()) {
        runnableQueue_.push(newQueue_.pop_all());
    }
}

} //namespace co
