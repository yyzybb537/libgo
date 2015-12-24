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

class CoTimer
{
public:
    typedef std::function<void()> fn_t;
    typedef std::chrono::time_point<std::chrono::high_resolution_clock> TimePoint;

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
    TimePoint next_time_point_;

    friend class CoTimerMgr;
};
typedef std::shared_ptr<CoTimer> CoTimerPtr;
typedef CoTimerPtr TimerId;

// 定时器管理
class CoTimerMgr
{
public:
    typedef std::map<uint64_t, CoTimerPtr> Timers;
    typedef std::chrono::time_point<std::chrono::high_resolution_clock> TimePoint;
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

    uint32_t GetExpired(std::list<CoTimerPtr> &result, uint32_t n = 1);

    static TimePoint Now();

private:
    void __Cancel(CoTimerPtr co_timer_ptr);

private:
    Timers timers_;
    DeadLines deadlines_;
    LFLock lock_;
};


} //namespace co
