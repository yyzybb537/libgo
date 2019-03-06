#include "co_mutex.h"
#include "../scheduler/scheduler.h"

namespace co
{

CoMutex::CoMutex()
{
    sem_ = 1;
}

CoMutex::~CoMutex()
{
//    assert(lock_.try_lock());
}

void CoMutex::lock()
{
    if (--sem_ == 0)
        return ;

    std::unique_lock<lock_t> lock(lock_);
    if (notified_) {
        notified_ = false;
        return ;
    }
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
    return sem_ < 1;
}

void CoMutex::unlock()
{
    if (++sem_ == 1)
        return ;

    std::unique_lock<lock_t> lock(lock_);
    if (!cv_.notify_one())
        notified_ = true;
}

} //namespace co
