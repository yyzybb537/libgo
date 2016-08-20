#include <libgo/processer.h>
#include <libgo/scheduler.h>
#include <libgo/error.h>
#include <assert.h>

namespace co {

atomic_t<uint32_t> Processer::s_id_{0};

Processer::Processer()
    : id_(++s_id_)
{
    runnable_list_.check_ = (void*)&s_id_;
}

void Processer::AddTaskRunnable(Task *tk)
{
    DebugPrint(dbg_scheduler, "task(%s) add into proc(%u)", tk->DebugInfo(), id_);
    tk->state_ = TaskState::runnable;
    runnable_list_.push(tk);
}

uint32_t Processer::Run(uint32_t &done_count)
{
    ContextScopedGuard guard;
    (void)guard;

    done_count = 0;
    uint32_t c = 0;

    DebugPrint(dbg_scheduler, "Run [Proc(%d) do_count:%u] --------------------------",
            id_, (uint32_t)runnable_list_.size());

    for (;;)
    {
        if (c >= runnable_list_.size()) break;
        Task *tk = runnable_list_.pop();
        if (!tk) break;
        ++c;

        current_task_ = tk;
        DebugPrint(dbg_switch, "enter task(%s)", tk->DebugInfo());
        if (!tk->SwapIn()) {
            fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
            current_task_ = nullptr;
            runnable_list_.erase(tk);
            tk->DecrementRef();
            ThrowError(eCoErrorCode::ec_swapcontext_failed);
        }
        DebugPrint(dbg_switch, "leave task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
        current_task_ = nullptr;

        switch (tk->state_) {
            case TaskState::runnable:
                runnable_list_.push(tk);
                break;

            case TaskState::io_block:
                g_Scheduler.io_wait_.SchedulerSwitch(tk);
                break;

            case TaskState::sleep:
                g_Scheduler.sleep_wait_.SchedulerSwitch(tk);
                break;

            case TaskState::sys_block:
                assert(tk->block_);
                if (!tk->block_->AddWaitTask(tk))
                    runnable_list_.push(tk);
                break;

            case TaskState::done:
            default:
                ++done_count;
                DebugPrint(dbg_task, "task(%s) done.", tk->DebugInfo());
                if (tk->eptr_) {
                    std::exception_ptr ep = tk->eptr_;
                    tk->DecrementRef();
                    std::rethrow_exception(ep);
                } else
                    tk->DecrementRef();
                break;
        }
    }

    return c;
}

void Processer::CoYield()
{
    Task *tk = GetCurrentTask();
    assert(tk);
    tk->proc_ = this;

    DebugPrint(dbg_yield, "yield task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
    ++tk->yield_count_;
    if (!tk->SwapOut()) {
        fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
        ThrowError(eCoErrorCode::ec_yield_failed);
    }
}

Task* Processer::GetCurrentTask()
{
    return current_task_;
}

std::size_t Processer::StealHalf(Processer & other)
{
    std::size_t runnable_task_count = runnable_list_.size();
    SList<Task> tasks = runnable_list_.pop_back((runnable_task_count + 1) / 2);
    std::size_t c = tasks.size();
    DebugPrint(dbg_scheduler, "proc[%u] steal proc[%u] work returns %d.",
            other.id_, id_, (int)c);
    if (!c) return 0;
    other.runnable_list_.push(std::move(tasks));
    return c;
}

} //namespace co
