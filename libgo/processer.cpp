#include "processer.h"
#include "scheduler.h"
#include "error.h"
#include "assert.h"

namespace co {

std::atomic<uint32_t> Processer::s_id_{0};

Processer::Processer()
    : id_(++s_id_)
{
    ts_runnable_list_.check_ = runnable_list_.check_;
}

void Processer::AddTaskRunnable(Task *tk)
{
    DebugPrint(dbg_scheduler, "task(%s) add into proc(%u)", tk->DebugInfo(), id_);
    if (tk->state_ == TaskState::init) {
        assert(!tk->proc_);
        tk->AddIntoProcesser(this);
        ++ task_count_;
    }

    assert(tk->proc_ == this);
    tk->state_ = TaskState::runnable;
    ts_runnable_list_.push(tk);
}

uint32_t Processer::Run(uint32_t &done_count)
{
    ContextScopedGuard guard;
    (void)guard;

    done_count = 0;
    uint32_t c = 0;

    if (!ts_runnable_list_.empty_without_lock())
        runnable_list_.push(ts_runnable_list_.pop_all());

//    DebugPrint(dbg_scheduler, "Run [Proc(%d) do_count:%u] --------------------------",
//            id_, (uint32_t)runnable_list_.size());

    Task *pos = (Task*)runnable_list_.head_->next;
    while (pos) {
        ++c;
        Task* tk = pos;
        pos = (Task*)pos->next;

        current_task_ = tk;
//        DebugPrint(dbg_switch, "enter task(%s)", tk->DebugInfo());
        if (!tk->SwapIn()) {
            fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
            current_task_ = nullptr;
            runnable_list_.erase(tk);
            tk->DecrementRef();
            ThrowError(eCoErrorCode::ec_swapcontext_failed);
        }
//        DebugPrint(dbg_switch, "leave task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
        current_task_ = nullptr;

        switch (tk->state_) {
            case TaskState::runnable:
                break;

            case TaskState::io_block:
                runnable_list_.erase(tk);
                g_Scheduler.io_wait_.SchedulerSwitch(tk);
                break;

            case TaskState::sleep:
                runnable_list_.erase(tk);
                g_Scheduler.sleep_wait_.SchedulerSwitch(tk);
                break;

            case TaskState::sys_block:
                assert(tk->block_);
                runnable_list_.erase(tk);
                if (!tk->block_->AddWaitTask(tk))
                    runnable_list_.push(tk);
                break;

            case TaskState::done:
            default:
                --task_count_;
                ++done_count;
                runnable_list_.erase(tk);
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
    assert(tk->proc_ == this);

    DebugPrint(dbg_yield, "yield task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
    ++tk->yield_count_;
    if (!tk->SwapOut()) {
        fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
        ThrowError(eCoErrorCode::ec_yield_failed);
    }
}

uint32_t Processer::GetTaskCount()
{
    return task_count_;
}

Task* Processer::GetCurrentTask()
{
    return current_task_;
}

} //namespace co
