#pragma once
#include <map>
#include <functional>
#include <chrono>
#include <memory>
#include <vector>
#include <list>
#include "spinlock.h"

namespace co
{

class CoTimer;
typedef std::chrono::time_point<std::chrono::high_resolution_clock> TimePoint;
typedef std::shared_ptr<CoTimer> CoTimerPtr;

class CoTimer
{
public:
    typedef std::function<void()> fn_t;
    typedef std::multimap<TimePoint, CoTimerPtr>::iterator Token;

    explicit CoTimer(fn_t const& fn);
    uint64_t GetId();
    void operator()();
    bool Cancel();
    bool BlockCancel();

private:
    uint64_t id_;
    static std::atomic<uint64_t> s_id;
    fn_t fn_;
    bool active_;
    LFLock fn_lock_;
    Token token_;

    friend class CoTimerMgr;
};
typedef std::shared_ptr<CoTimer> CoTimerPtr;
typedef CoTimerPtr TimerId;

// 定时器管理
class CoTimerMgr
{
public:
    typedef std::multimap<TimePoint, CoTimerPtr> DeadLines;

    CoTimerMgr();
	~CoTimerMgr();

    CoTimerPtr ExpireAt(TimePoint const& time_point, CoTimer::fn_t const& fn);

    template <typename Duration>
    CoTimerPtr ExpireAt(Duration const& duration, CoTimer::fn_t const& fn)
    {
        return ExpireAt(Now() + duration, fn);
    }

    bool Cancel(CoTimerPtr co_timer_ptr);
    bool BlockCancel(CoTimerPtr co_timer_ptr);

    // @returns: 下一个触发的timer时间(单位: milliseconds)
    long long GetExpired(std::list<CoTimerPtr> &result, uint32_t n = 1);

    static TimePoint Now();

private:
    void __Cancel(CoTimerPtr co_timer_ptr);

    long long GetNextTriggerTime();

    void SetNextTriggerTime(TimePoint const& tp);

private:
    DeadLines deadlines_;
    LFLock lock_;

    // 定时器创建时的时间点, 作为时间基准
    TimePoint zero_time_;

    // 下一个timer触发的时间
    //  单位: milliseconds, 0时刻基准点: zero_time_
    // 这个值由GetExpired时成功lock的线程来设置, 未lock成功的线程也允许读取.
    std::atomic<long long> next_trigger_time_;
};

} //namespace co
