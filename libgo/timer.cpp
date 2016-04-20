#include "timer.h"
#include <mutex>
#include <limits>

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
    : next_trigger_time_{std::numeric_limits<long>::max()}
{
    zero_time_ = Now();
}

CoTimerMgr::~CoTimerMgr()
{

}

CoTimerPtr CoTimerMgr::ExpireAt(TimePoint const& time_point, CoTimer::fn_t const& fn)
{
    std::unique_lock<LFLock> lock(lock_);
    CoTimerPtr sptr(new CoTimer(fn));
    if (deadlines_.empty())
        SetNextTriggerTime(time_point);
    sptr->token_ = deadlines_.insert(std::make_pair(time_point, sptr));
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
    std::unique_lock<LFLock> lock(lock_);
    deadlines_.erase(co_timer_ptr->token_);
}

long long CoTimerMgr::GetExpired(std::list<CoTimerPtr> &result, uint32_t n)
{
    std::unique_lock<LFLock> lock(lock_, std::defer_lock);
    if (!lock.try_lock()) return GetNextTriggerTime();

    TimePoint now = Now();
    auto it = deadlines_.begin();
    for (; it != deadlines_.end() && n > 0; --n, ++it)
    {
        if (it->first > now) {
            break;
        }

        result.push_back(it->second);
    }

    if (it != deadlines_.end()) {
        // 还有timer需要触发
        TimePoint const& next_tp = it->first;
        SetNextTriggerTime(next_tp);
    } else
        next_trigger_time_ = std::numeric_limits<long>::max();

    deadlines_.erase(deadlines_.begin(), it);
    return GetNextTriggerTime();
}

TimePoint CoTimerMgr::Now()
{
    return TimePoint::clock::now();
}

long long CoTimerMgr::GetNextTriggerTime()
{
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            Now() - zero_time_).count();
    if (next_trigger_time_ > now_ms)
        return next_trigger_time_ - now_ms;

    return 0;
}

void CoTimerMgr::SetNextTriggerTime(TimePoint const& tp)
{
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp - zero_time_).count();
    next_trigger_time_ = now_ms;
}

} //namespace co
