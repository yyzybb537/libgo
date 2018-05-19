#include <libgo/processer.h>
#include <libgo/scheduler.h>
#include <libgo/error.h>
#include <assert.h>

namespace co {

atomic_t<uint32_t> Processer::s_id_{0};

Processer::Processer()
    : id_(++s_id_)
{
    for (int i = 0; i < (int)RunnableListType::count; ++i)
        runnable_list_[i].check_ = (void*)&s_id_;
}

void Processer::AddTaskRunnable(Task *tk)
{
    DebugPrint(dbg_scheduler, "task(%s) add into proc(%u)", tk->DebugInfo(), id_);
    auto state = tk->state_;
    tk->state_ = TaskState::runnable;
    if (tk->is_affinity_) {
        runnable_list_[(int)RunnableListType::pri].push(tk);
    } else if (state == TaskState::io_block) {
        runnable_list_[(int)RunnableListType::io_trigger].push(tk);
    } else {
        runnable_list_[(int)RunnableListType::normal].push(tk);
    }
}

uint32_t Processer::Run(uint32_t &done_count)
{
    ContextScopedGuard guard;
    (void)guard;

    done_count = 0;
    uint32_t total = 0;

    DebugPrint(dbg_scheduler, "Run [Proc(%d) do_count:%u] --------------------------",
            id_, (uint32_t)size());

    for (int i = 0; i < (int)RunnableListType::count; ++i)
    {
        auto &the_list = runnable_list_[i];
        uint32_t c = 0;
        for (;;)
        {
            if (c >= the_list.size()) break;
            Task *tk = the_list.pop();
            if (!tk) break;
            ++c;

            current_task_ = tk;
            DebugPrint(dbg_switch, "enter task(%s)", tk->DebugInfo());
            if (!tk->SwapIn()) {
                fprintf(stderr, "swapcontext error:%s\n", strerror(errno));
                current_task_ = nullptr;
                the_list.erase(tk);
                tk->DecrementRef();
                ThrowError(eCoErrorCode::ec_swapcontext_failed);
            }
            DebugPrint(dbg_switch, "leave task(%s) state=%d", tk->DebugInfo(), (int)tk->state_);
            current_task_ = nullptr;

            switch (tk->state_) {
                case TaskState::runnable:
                    runnable_list_[(int)RunnableListType::normal].push(tk);
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
                        AddTaskRunnable(tk);
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
        total += c;
    }

    return total;
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
    std::size_t runnable_task_count = runnable_list_[(int)RunnableListType::normal].size();
    std::size_t steal_count = (runnable_task_count + 1) / 2;
    steal_count = (std::min)(steal_count, std::size_t(1024));
    SList<Task> tasks = runnable_list_[(int)RunnableListType::normal].pop_back(steal_count);
    std::size_t c = tasks.size();
    DebugPrint(dbg_scheduler, "proc[%u] steal proc[%u] work returns %d.",
            other.id_, id_, (int)c);
    if (!c) return 0;
    other.runnable_list_[(int)RunnableListType::normal].push(std::move(tasks));
    return c;
}

std::size_t Processer::size()
{
    std::size_t r = 0;
    for (int i = 0; i < (int)RunnableListType::count; ++i)
        r += runnable_list_[i].size();
    return r;
}

} //namespace co
