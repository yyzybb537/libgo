#include "processer.h"
#include "scheduler.h"
#include "error.h"
#include "assert.h"

namespace co {

std::atomic<uint32_t> Processer::s_id_{0};

Processer::Processer(uint32_t stack_size)
    : id_(++s_id_)
{
    shared_stack_cap_ = stack_size;
#if defined(ENABLE_SHARED_STACK)
    shared_stack_ = new char[shared_stack_cap_];
#endif
}
Processer::~Processer()
{
    if (shared_stack_) {
        delete[] shared_stack_;
        shared_stack_ = NULL;
    }
}

void Processer::AddTaskRunnable(Task *tk)
{
    DebugPrint(dbg_scheduler, "task(%s) add into proc(%u)", tk->DebugInfo(), id_);
    if (tk->state_ == TaskState::init) {
        assert(!tk->proc_);
        tk->AddIntoProcesser(this, shared_stack_, shared_stack_cap_);
        if (tk->state_ == TaskState::fatal) {
            // 创建失败
            tk->DecrementRef();
            throw std::system_error(errno, std::system_category());
        }
        ++ task_count_;
    }

    assert(tk->proc_ == this);
    tk->state_ = TaskState::runnable;
    runnable_list_.push(tk);
}

uint32_t Processer::Run(ThreadLocalInfo &info, uint32_t &done_count)
{
    ContextScopedGuard guard;

    info.current_task = NULL;
    done_count = 0;
    uint32_t c = 0;
    SList<Task> slist(runnable_list_.pop_all());
    uint32_t do_count = slist.size();

    DebugPrint(dbg_scheduler, "Run [Proc(%d) do_count:%u] --------------------------", id_, do_count);

    SList<Task>::iterator it = slist.begin();
    for (; it != slist.end(); ++c)
    {
        Task* tk = &*it;
        info.current_task = tk;
        tk->state_ = TaskState::runnable;
        DebugPrint(dbg_switch, "enter task(%s)", tk->DebugInfo());
        if (!tk->SwapIn()) {
            fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
            runnable_list_.push(tk);
            ThrowError(eCoErrorCode::ec_swapcontext_failed);
        }
        DebugPrint(dbg_switch, "leave task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
        info.current_task = NULL;

        switch (tk->state_) {
            case TaskState::runnable:
                ++it;
                break;

            case TaskState::io_block:
                it = slist.erase(it);
                g_Scheduler.io_wait_.SchedulerSwitch(tk);
                break;

            case TaskState::sleep:
                it = slist.erase(it);
                g_Scheduler.sleep_wait_.SchedulerSwitch(tk);
                break;

            case TaskState::sys_block:
                {
                    assert(tk->block_);
                    if (tk->block_) {
                        it = slist.erase(it);
                        if (!tk->block_->AddWaitTask(tk))
                            runnable_list_.push(tk);
                    }
                }
                break;

            case TaskState::done:
            default:
                --task_count_;
                ++done_count;
                it = slist.erase(it);
                DebugPrint(dbg_task, "task(%s) done.", tk->DebugInfo());
                if (tk->eptr_) {
                    std::exception_ptr ep = tk->eptr_;
                    runnable_list_.push(std::move(slist));
                    tk->DecrementRef();
                    std::rethrow_exception(ep);
                } else
                    tk->DecrementRef();
                break;
        }
    }

    runnable_list_.push(std::move(slist));
    return c;
}

void Processer::CoYield(ThreadLocalInfo &info)
{
    Task *tk = info.current_task;
    if (!tk) return ;

    DebugPrint(dbg_yield, "yield task(%s) state=%d", tk->DebugInfo(), tk->state_);
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

} //namespace co
