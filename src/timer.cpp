#include "timer.h"
#include <mutex>

namespace co
{

std::atomic<uint64_t> CoTimer::s_id{0};

CoTimer::CoTimer(fn_t const& fn)
    : id_(++s_id), fn_(fn), active_(true)
{}

uint64_t CoTimer::GetId()
{
    return id_;
}

void CoTimer::operator()()
{
    std::unique_lock<LFLock> lock(fn_lock_, std::defer_lock);
    if (!lock.try_lock()) return ;

    // below statement was locked.
    if (!active_) return ;
    active_ = false;
    fn_();
}

bool CoTimer::Cancel()
{
    std::unique_lock<LFLock> lock(fn_lock_, std::defer_lock);
    if (!lock.try_lock()) return false;

    // below statement was locked.
    if (!active_) return false;
    active_ = false;
    return true;
}

bool CoTimer::BlockCancel()
{
    std::unique_lock<LFLock> lock(fn_lock_);

    // below statement was locked.
    if (!active_) return false;
    active_ = false;
    return true;
}

CoTimerMgr::CoTimerMgr()
{}

CoTimerMgr::~CoTimerMgr()
{

}

CoTimerPtr CoTimerMgr::ExpireAt(TimePoint const& time_point, CoTimer::fn_t const& fn)
{
    std::unique_lock<LFLock> lock(lock_);
    CoTimerPtr sptr(new CoTimer(fn));
    sptr->next_time_point_ = time_point;
    timers_[sptr->GetId()] = sptr;
    deadlines_.insert(std::make_pair(time_point, sptr));
    return sptr;
}

bool CoTimerMgr::Cancel(CoTimerPtr co_timer_ptr)
{
    if (!co_timer_ptr->Cancel()) return false;
    __Cancel(co_timer_ptr);
    return true;
}

bool CoTimerMgr::BlockCancel(CoTimerPtr co_timer_ptr)
{
    if (!co_timer_ptr->BlockCancel()) return false;
    __Cancel(co_timer_ptr);
    return true;
}

void CoTimerMgr::__Cancel(CoTimerPtr co_timer_ptr)
{
    uint64_t timer_id = co_timer_ptr->GetId();

    std::unique_lock<LFLock> lock(lock_);
    Timers::iterator timers_it = timers_.find(timer_id);
    if (timers_.end() == timers_it)
        return ;

    timers_.erase(timers_it);

    auto range = deadlines_.equal_range(co_timer_ptr->next_time_point_);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == co_timer_ptr) {
            deadlines_.erase(it);
            break;
        }
    }
}

uint32_t CoTimerMgr::GetExpired(std::list<CoTimerPtr> &result, uint32_t n)
{
    std::unique_lock<LFLock> lock(lock_);
    TimePoint now = Now();
    auto it = deadlines_.begin();
    for (; it != deadlines_.end() && n > 0; --n, ++it)
    {
        if (it->first > now) {
            break;
        }

        result.push_back(it->second);
        timers_.erase(it->second->GetId());
    }

    deadlines_.erase(deadlines_.begin(), it);
    return result.size();
}

CoTimerMgr::TimePoint CoTimerMgr::Now()
{
    return TimePoint::clock::now();
}


} //namespace co
