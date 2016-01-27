#include "block_object.h"
#include "scheduler.h"
#include "error.h"
#include <unistd.h>
#include <mutex>
#include <limits>

namespace co
{

BlockObject::BlockObject(std::size_t init_wakeup, std::size_t max_wakeup)
    : wakeup_(init_wakeup), max_wakeup_(max_wakeup)
{}

BlockObject::~BlockObject()
{
    if (!lock_.try_lock()) {
        ThrowError(eCoErrorCode::ec_block_object_locked);
    }

    if (!wait_queue_.empty()) {
        ThrowError(eCoErrorCode::ec_block_object_waiting);
    }
}

void BlockObject::CoBlockWait()
{
    if (!g_Scheduler.IsCoroutine()) {
        while (!TryBlockWait()) usleep(10 * 1000);
        return ;
    }

    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_ > 0) {
        DebugPrint(dbg_syncblock, "wait immedaitely done.");
        --wakeup_;
        return ;
    }
    lock.unlock();

    Task* tk = g_Scheduler.GetLocalInfo().current_task;
    tk->block_ = this;
    tk->state_ = TaskState::sys_block;
    tk->block_timeout_ = std::chrono::nanoseconds::zero();
    tk->is_block_timeout_ = false;
    ++ tk->block_sequence_;
    DebugPrint(dbg_syncblock, "wait to switch. task(%s)", tk->DebugInfo());
    g_Scheduler.CoYield();
}

bool BlockObject::CoBlockWaitTimed(std::chrono::nanoseconds timeo)
{
    auto begin = std::chrono::high_resolution_clock::now();
    if (!g_Scheduler.IsCoroutine()) {
        while (!TryBlockWait() &&
                std::chrono::duration_cast<std::chrono::nanoseconds>
                (std::chrono::high_resolution_clock::now() - begin) < timeo)
            usleep(10 * 1000);
        return false;
    }

    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_ > 0) {
        DebugPrint(dbg_syncblock, "wait immedaitely done.");
        --wakeup_;
        return true;
    }
    lock.unlock();

    Task* tk = g_Scheduler.GetLocalInfo().current_task;
    tk->block_ = this;
    tk->state_ = TaskState::sys_block;
    ++tk->block_sequence_;
    tk->block_timeout_ = timeo;
    tk->is_block_timeout_ = false;
    DebugPrint(dbg_syncblock, "wait to switch. task(%s)", tk->DebugInfo());
    g_Scheduler.CoYield();

    return !tk->is_block_timeout_;
}

bool BlockObject::TryBlockWait()
{
    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_ == 0)
        return false;

    --wakeup_;
    DebugPrint(dbg_syncblock, "try wait success.");
    return true;
}

bool BlockObject::Wakeup()
{
    std::unique_lock<LFLock> lock(lock_);
    Task* tk = wait_queue_.pop();
    if (!tk) {
        if (wakeup_ >= max_wakeup_) {
            DebugPrint(dbg_syncblock, "wakeup failed.");
            return false;
        }

        ++wakeup_;
        DebugPrint(dbg_syncblock, "wakeup to %lu.", (long unsigned)wakeup_);
        return true;
    }

    tk->block_ = nullptr;
    g_Scheduler.AddTaskRunnable(tk);
    DebugPrint(dbg_syncblock, "wakeup task(%s).", tk->DebugInfo());
    return true;
}
void BlockObject::CancelWait(Task* tk, uint32_t block_sequence)
{
    std::unique_lock<LFLock> lock(lock_);
    if (tk->block_ != this) {
        DebugPrint(dbg_syncblock, "cancelwait task(%s) failed. tk->block_ is not this!", tk->DebugInfo());
        return;
    }

    if (tk->block_sequence_ != block_sequence) {
        DebugPrint(dbg_syncblock, "cancelwait task(%s) failed. tk->block_sequence_ = %u, block_sequence = %u.",
                tk->DebugInfo(), tk->block_sequence_, block_sequence);
        return;
    }

    if (!wait_queue_.erase(tk)) {
        DebugPrint(dbg_syncblock, "cancelwait task(%s) erase failed.", tk->DebugInfo());
        return;
    }

    tk->block_ = nullptr;
    tk->is_block_timeout_ = true;
    g_Scheduler.AddTaskRunnable(tk);
    DebugPrint(dbg_syncblock, "cancelwait task(%s).", tk->DebugInfo());
}

bool BlockObject::IsWakeup()
{
    std::unique_lock<LFLock> lock(lock_);
    return wakeup_ > 0;
}

bool BlockObject::AddWaitTask(Task* tk)
{
    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_) {
        --wakeup_;
        return false;
    }

    wait_queue_.push(tk);

    // 带超时的, 增加定时器
    if (std::chrono::nanoseconds::zero() != tk->block_timeout_) {
        uint32_t seq = tk->block_sequence_;
        tk->IncrementRef();
        lock.unlock(); // sequence记录完成, task引用计数增加, 可以解锁了

        g_Scheduler.ExpireAt(tk->block_timeout_, [=]{
                if (tk->block_sequence_ == seq)
                    this->CancelWait(tk, seq);
                tk->DecrementRef();
                });
    }

    return true;
}

} //namespace co
