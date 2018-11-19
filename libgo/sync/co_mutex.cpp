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
    assert(!isLocked_);
}

void CoMutex::lock()
{
    std::unique_lock<LFLock> lock(lock_);

retry:
    if (!isLocked_) {
        isLocked_ = true;
        return ;
    }

    cv_.wait(lock);
    goto retry;
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
    isLocked_ = false;
    cv_.notify_one();
    return ;
}

} //namespace co
