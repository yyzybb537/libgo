#include "co_mutex.h"
#include "../scheduler/scheduler.h"

namespace co
{

CoMutex::CoMutex() : isLocked_(false)
{
}

CoMutex::~CoMutex()
{
    assert(lock_.try_lock());
    assert(queue_.empty());
    assert(!isLocked_);
}

void CoMutex::lock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (!isLocked_) {
        isLocked_ = true;
        return ;
    }

    if (Processer::IsCoroutine()) {
        // 协程
        queue_.push(Processer::Suspend());
        lock.unlock();
        Processer::StaticCoYield();
    } else {
        // 原生线程
        queue_.push(Processer::SuspendEntry{});
        cv_.wait(lock);
    }
}

bool CoMutex::try_lock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (!isLocked_) {
        isLocked_ = true;
        return true;
    }
    return false;
}

bool CoMutex::is_lock()
{
    std::unique_lock<LFLock> lock(lock_);
    return isLocked_;
}

void CoMutex::unlock()
{
    std::unique_lock<LFLock> lock(lock_);
    assert(isLocked_);
    
    while (!queue_.empty()) {
        auto entry = queue_.front();
        queue_.pop();

        if (entry) {
            // 协程
            if (Processer::Wakeup(entry))
                return ;
        } else {
            // 原生线程
            cv_.notify_one();
            return ;
        }
    }

    isLocked_ = false;
    return ;
}

} //namespace co
