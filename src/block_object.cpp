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

    Task* tk = g_Scheduler.GetLocalInfo().current_task;
    tk->block_ = this;
    tk->state_ = TaskState::sys_block;
    lock.unlock();
    DebugPrint(dbg_syncblock, "wait to switch. task(%s)", tk->DebugInfo());
    g_Scheduler.CoYield();
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

    g_Scheduler.AddTaskRunnable(tk);
    DebugPrint(dbg_syncblock, "wakeup task(%s).", tk->DebugInfo());
    return true;
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
    return true;
}

} //namespace co
