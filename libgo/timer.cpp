#include <libgo/timer.h>
#include <mutex>
#include <limits>
#include <algorithm>

namespace co
{

atomic_t<uint64_t> CoTimer::s_id{0};

CoTimer::CoTimer(fn_t const& fn)
    : id_(++s_id), fn_(fn), active_(true), token_state_(e_token_state::none)
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
    : system_next_trigger_time_{std::numeric_limits<long long>::max()},
    steady_next_trigger_time_{std::numeric_limits<long long>::max()}
{
}

CoTimerPtr CoTimerMgr::ExpireAt(SystemTimePoint const& time_point,
        CoTimer::fn_t const& fn)
{
    std::unique_lock<LFLock> lock(lock_);
    CoTimerPtr sptr(new CoTimer(fn));
    if (system_deadlines_.empty() && steady_deadlines_.empty())
        SetNextTriggerTime(time_point);
    sptr->token_state_ = CoTimer::e_token_state::system;
    sptr->system_token_ = system_deadlines_.insert(std::make_pair(time_point, sptr));
    return sptr;
}

#ifndef UNSUPPORT_STEADY_TIME
CoTimerPtr CoTimerMgr::ExpireAt(SteadyTimePoint const& time_point,
        CoTimer::fn_t const& fn)
{
    std::unique_lock<LFLock> lock(lock_);
    CoTimerPtr sptr(new CoTimer(fn));
    if (system_deadlines_.empty() && steady_deadlines_.empty())
        SetNextTriggerTime(time_point);
    sptr->token_state_ = CoTimer::e_token_state::steady;
    sptr->steady_token_ = steady_deadlines_.insert(std::make_pair(time_point, sptr));
    return sptr;
}
#endif

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
    switch (co_timer_ptr->token_state_) {
        case CoTimer::e_token_state::system:
            system_deadlines_.erase(co_timer_ptr->system_token_);
            break;

        case CoTimer::e_token_state::steady:
            steady_deadlines_.erase(co_timer_ptr->steady_token_);
            break;

        case CoTimer::e_token_state::none:
        default:
            return;
    }

    co_timer_ptr->token_state_ = CoTimer::e_token_state::none;
}

long long CoTimerMgr::GetExpired(std::list<CoTimerPtr> &result, uint32_t n)
{
    if (system_deadlines_.empty() && steady_deadlines_.empty())
        return std::numeric_limits<long long>::max();

    std::unique_lock<LFLock> lock(lock_, std::defer_lock);
    if (!lock.try_lock()) return GetNextTriggerTime();

    {
        SystemTimePoint now = SystemNow();
        auto it = system_deadlines_.begin();
        for (; it != system_deadlines_.end() && n > 0; --n, ++it)
        {
            if (it->first > now) {
                break;
            }

            it->second->token_state_ = CoTimer::e_token_state::none;
            result.push_back(it->second);
        }
        if (it != system_deadlines_.end())
            SetNextTriggerTime(it->first);
        else
            system_next_trigger_time_ = std::numeric_limits<long long>::max();
        system_deadlines_.erase(system_deadlines_.begin(), it);
    }

    {
        SteadyTimePoint now = SteadyNow();
        auto it = steady_deadlines_.begin();
        for (; it != steady_deadlines_.end() && n > 0; --n, ++it)
        {
            if (it->first > now) {
                break;
            }

            it->second->token_state_ = CoTimer::e_token_state::none;
            result.push_back(it->second);
        }
        if (it != steady_deadlines_.end())
            SetNextTriggerTime(it->first);
        else
            steady_next_trigger_time_ = std::numeric_limits<long long>::max();
        steady_deadlines_.erase(steady_deadlines_.begin(), it);
    }

    return GetNextTriggerTime();
}

std::size_t CoTimerMgr::Size()
{
    std::unique_lock<LFLock> lock(lock_);
    return system_deadlines_.size() + steady_deadlines_.size();
}

long long CoTimerMgr::GetNextTriggerTime()
{
    long long sys_now = std::chrono::time_point_cast<std::chrono::milliseconds>(SystemNow()).time_since_epoch().count();
    long long sdy_now = std::chrono::time_point_cast<std::chrono::milliseconds>(SteadyNow()).time_since_epoch().count();

    long long sys_delta = (std::max)(system_next_trigger_time_ - sys_now, (long long)0);
    long long sdy_delta = (std::max)(steady_next_trigger_time_ - sdy_now, (long long)0);

    return (std::min)(sys_delta, sdy_delta);
}

void CoTimerMgr::SetNextTriggerTime(SystemTimePoint const& sys_tp)
{
    system_next_trigger_time_ = std::chrono::time_point_cast<std::chrono::milliseconds>(sys_tp).time_since_epoch().count();
}

#ifndef UNSUPPORT_STEADY_TIME
void CoTimerMgr::SetNextTriggerTime(SteadyTimePoint const& sdy_tp)
{
    steady_next_trigger_time_ = std::chrono::time_point_cast<std::chrono::milliseconds>(sdy_tp).time_since_epoch().count();
}
#endif

} //namespace co
