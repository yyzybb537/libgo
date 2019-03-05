#include "co_mutex.h"
#include "../scheduler/scheduler.h"

namespace co
{

CoMutex::CoMutex() : sem_(1)
{
}

CoMutex::~CoMutex()
{
    assert(lock_.try_lock());
    assert(sem_ == 1);
}

void CoMutex::lock()
{
    if (--sem_ == 0)
        return ;

    std::unique_lock<LFLock> lock(lock_);
    cv_.wait(lock);
}

bool CoMutex::try_lock()
{
    if (--sem_ == 0)
        return true;

    ++sem_;
    return false;
}

bool CoMutex::is_lock()
{
    return sem_ != 1;
}

void CoMutex::unlock()
{
    long val = ++sem_;
    assert(val <= 1);
    if (val < 1) {
        std::unique_lock<LFLock> lock(lock_);
        cv_.notify_one();
    }
}

} //namespace co
